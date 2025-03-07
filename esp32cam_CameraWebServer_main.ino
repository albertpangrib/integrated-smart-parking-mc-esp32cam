#include "esp_camera.h"
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"
#include <FirebaseClient.h>
#include <WiFiClientSecure.h>

#define LED_GPIO_NUM 4
#define RED_PIN_LED 15
#define GREEN_PIN_LED 14
#define BLUE_PIN_LED 2
constexpr int TRIG_PIN = 12;
constexpr int ECHO_PIN = 13;

String ID_DEVICE = "2";
String pathFirebase = "esp32cam/slot_" + ID_DEVICE;
int ip = 102;

void authHandler();

void printResult(AsyncResult &aResult);

void printError(int code, const String &msg);
DefaultNetwork network;
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client, getNetwork(network));
RealtimeDatabase Database;
AsyncResult aResult_no_callback;
IPAddress local_IP(192, 168, 189, ip);
IPAddress gateway(192, 168, 189, 2);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

TaskHandle_t TaskFirebaseGet;
TaskHandle_t TaskFirebaseUpdate;
String is_booked = "false";
int currentStatus = -1;
int lastStatus = -1;

void startCameraServer();

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Gagal mengatur Static IP untuk ESP32-CAM 1");
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);
  Serial.print("Menghubungkan ke WiFi");
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\nWiFi terhubung!" : "\nGagal terhubung ke WiFi!");
}

void initializeFirebase() {
  Serial.printf("Firebase Client v%s\n", FIREBASE_CLIENT_VERSION);
  Serial.println("Initializing Firebase...");
  ssl_client.setInsecure();
  initializeApp(aClient, app, getAuth(user_auth), aResult_no_callback);
  authHandler();
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);
  aClient.setAsyncResult(aResult_no_callback);
}

float getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH);
  return duration * 0.034 / 2;
}

void convertDistance(int &status) {
  float distance = getDistance();
  status = (distance <= 100) ? 1 : 2;
  Serial.printf("Jarak: %.2f cm, Status: %d\n", distance, status);
}

void updateLED(int status, String bookedStatus) {
  bool redLEDOn = (status == 1 || bookedStatus == "true");
  bool greenLEDOn = (status == 2 && bookedStatus == "false");

  digitalWrite(RED_PIN_LED, redLEDOn ? HIGH : LOW);
  digitalWrite(GREEN_PIN_LED, greenLEDOn ? HIGH : LOW);

  Serial.printf("Status: %d, Booked: %s, Merah: %d, Hijau: %d\n",
                status, bookedStatus.c_str(), redLEDOn, greenLEDOn);
}

void getFirebaseData(void *pvParameters) {
  while (true) {
    String getFirebasePath = pathFirebase + "/isBooked";
    String new_value = Database.get<String>(aClient, getFirebasePath);
    if (!new_value.isEmpty() && new_value != is_booked) {
      is_booked = new_value;
      Serial.println("Data dari DB diperbarui: " + is_booked);
      updateLED(currentStatus, is_booked);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void updateSensorFB(int status) {
  String updateFBSensorPath = pathFirebase;
  Serial.print("Mengirim data ke Firebase... ");
  object_t json;
  JsonWriter writer;
  writer.create(json, "/sensorData", status);
  Serial.print("Updating data Sensor... ");
  bool updateStatus = Database.update(aClient, updateFBSensorPath, json);

  if (updateStatus)
    Serial.println("ok");
  else
    printError(aClient.lastError().code(), aClient.lastError().message());
}

void updateFirebaseTask(void *pvParameters) {
  while (true) {
    convertDistance(currentStatus);
    if (currentStatus != lastStatus) {
      updateSensorFB(currentStatus);
      lastStatus = currentStatus;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void updateIPAddressFirebase(String ipAddress) {
  String updateIPFirebasePath = pathFirebase;
  Serial.print("Mengirim data IP Address ke Firebase... ");
  object_t json;
  JsonWriter writer;
  writer.create(json, "/ipAddress", ipAddress);
  Serial.print("Updating data IP Address... ");
  bool updateIPAddress = Database.update(aClient, updateIPFirebasePath, json);

  if (updateIPAddress)
    Serial.println("ok");
  else
    printError(aClient.lastError().code(), aClient.lastError().message());
}

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_HD;
  config.pixel_format = PIXFORMAT_JPEG;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
  }
  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_HD);
  s->set_brightness(s, 1);
  s->set_contrast(s, 1);
  s->set_saturation(s, 1);
  s->set_vflip(s, 0);
  s->set_hmirror(s, 0);
}

void setupPins() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(RED_PIN_LED, OUTPUT);
  pinMode(GREEN_PIN_LED, OUTPUT);
  pinMode(BLUE_PIN_LED, OUTPUT);
  pinMode(LED_GPIO_NUM, OUTPUT);

  digitalWrite(RED_PIN_LED, LOW);
  digitalWrite(GREEN_PIN_LED, LOW);
  digitalWrite(BLUE_PIN_LED, LOW);
  digitalWrite(LED_GPIO_NUM, LOW);
}

void setup() {
  Serial.begin(115200);
  connectWiFi();
  delay(1000);
  setupPins();
  setupCamera();
  startCameraServer();
  String ipAddress = WiFi.localIP().toString();
  Serial.printf("Camera Ready! Use 'http://%s' to connect\n", ipAddress.c_str());
  digitalWrite(LED_GPIO_NUM, HIGH);
  delay(2000);
  digitalWrite(LED_GPIO_NUM, LOW);
  initializeFirebase();
  digitalWrite(BLUE_PIN_LED, HIGH);
  delay(2000);
  digitalWrite(BLUE_PIN_LED, LOW);

  updateIPAddressFirebase(ipAddress);

  xTaskCreatePinnedToCore(getFirebaseData, "TaskFirebaseGet", 8192, NULL, 1, &TaskFirebaseGet, 0);
  xTaskCreatePinnedToCore(updateFirebaseTask, "TaskFirebaseUpdate", 8192, NULL, 1, &TaskFirebaseUpdate, 1);
}

void loop() {
  Database.loop();
  updateLED(currentStatus, is_booked);
  delay(1000);
}

void authHandler() {
  unsigned long ms = millis();
  while (app.isInitialized() && !app.ready() && millis() - ms < 120 * 1000) {
    // The JWT token processor required for ServiceAuth and CustomAuth authentications.
    // JWT is a static object of JWTClass and it's not thread safe.
    // In multi-threaded operations (multi-FirebaseApp), you have to define JWTClass for each FirebaseApp,
    // and set it to the FirebaseApp via FirebaseApp::setJWTProcessor(<JWTClass>), before calling initializeApp.
    // JWT.loop(app.getAuth());
    printResult(aResult_no_callback);
  }
}

void printResult(AsyncResult &aResult) {
  if (aResult.isEvent()) {
    Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.appEvent().message().c_str(), aResult.appEvent().code());
  }

  if (aResult.isDebug()) {
    Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());
  }

  if (aResult.isError()) {
    Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());
  }
}

void printError(int code, const String &msg) {
  Firebase.printf("Error, msg: %s, code: %d\n", msg.c_str(), code);
}