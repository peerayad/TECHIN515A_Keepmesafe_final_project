#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FirebaseClient.h>
#include "esp_camera.h"
#include "esp_task_wdt.h"

// Edge Impulse ML
#include <ML_Camera_inferencing.h>

// ==========================================
// PIN DEFINITIONS
// ==========================================
#define PIR_PIN    D9
#define LED_PIN    D8
#define BUZZER_PIN D0
#define SDA_PIN    D4
#define SCL_PIN    D5

// ==========================================
// FIREBASE CONFIG
// ==========================================
#define FIREBASE_DATABASE_URL "https://keepmesafe-1e248-default-rtdb.firebaseio.com"
#define ROOM_ID               "room304"

// ==========================================
// OLED
// ==========================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_ADDRESS  0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==========================================
// FIREBASE
// ==========================================
WiFiClientSecure ssl;
DefaultNetwork network;
AsyncClientClass aClient(ssl, getNetwork(network));
FirebaseApp app;
RealtimeDatabase Database;
NoAuth noAuth;

// ==========================================
// CAMERA PINS (XIAO ESP32S3 Sense)
// ==========================================
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39
#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13

// ==========================================
// STATE
// ==========================================
bool          lastPIRState    = false;
int           alertCount      = 0;
bool          firebaseReady   = false;
unsigned long lastCaptureTime = 0;
#define CAPTURE_COOLDOWN 10000

volatile bool alarmLED        = false;
volatile bool playMelody      = false;
TaskHandle_t  blinkTaskHandle = NULL;

// ==========================================
// JINGLE BELLS MELODY
// ==========================================
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523

const int jinglNotes[] = {
  NOTE_E4, NOTE_E4, NOTE_E4,
  NOTE_E4, NOTE_E4, NOTE_E4,
  NOTE_E4, NOTE_G4, NOTE_C4, NOTE_D4, NOTE_E4,
  NOTE_F4, NOTE_F4, NOTE_F4, NOTE_F4,
  NOTE_F4, NOTE_E4, NOTE_E4, NOTE_E4,
  NOTE_E4, NOTE_D4, NOTE_D4, NOTE_E4, NOTE_D4, NOTE_G4,
  NOTE_E4, NOTE_E4, NOTE_E4,
  NOTE_E4, NOTE_E4, NOTE_E4,
  NOTE_E4, NOTE_G4, NOTE_C4, NOTE_D4, NOTE_E4,
  NOTE_F4, NOTE_F4, NOTE_F4, NOTE_F4,
  NOTE_F4, NOTE_E4, NOTE_E4, NOTE_E4,
  NOTE_G4, NOTE_G4, NOTE_F4, NOTE_D4, NOTE_C4
};

const int jinglDur[] = {
  200, 200, 400,
  200, 200, 400,
  200, 200, 200, 200, 800,
  200, 200, 200, 200,
  200, 200, 200, 200,
  200, 200, 200, 200, 200, 800,
  200, 200, 400,
  200, 200, 400,
  200, 200, 200, 200, 800,
  200, 200, 200, 200,
  200, 200, 200, 200,
  200, 200, 200, 200, 800
};

void blinkTask(void* param) {
  int noteIndex = 0;
  int noteLen   = sizeof(jinglNotes) / sizeof(jinglNotes[0]);
  while (true) {
    if (alarmLED) {
      digitalWrite(LED_PIN, HIGH);
      vTaskDelay(300 / portTICK_PERIOD_MS);
      digitalWrite(LED_PIN, LOW);
      vTaskDelay(300 / portTICK_PERIOD_MS);
    } else if (playMelody) {
      if (noteIndex < noteLen) {
        tone(BUZZER_PIN, jinglNotes[noteIndex], jinglDur[noteIndex]);
        digitalWrite(LED_PIN, HIGH);
        vTaskDelay((int)(jinglDur[noteIndex] * 1.1) / portTICK_PERIOD_MS);
        noTone(BUZZER_PIN);
        digitalWrite(LED_PIN, LOW);
        noteIndex++;
      } else {
        playMelody = false;
        noteIndex  = 0;
        digitalWrite(LED_PIN, LOW);
        digitalWrite(BUZZER_PIN, LOW);
      }
    } else {
      noteIndex = 0;
      digitalWrite(LED_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }
}

// ==========================================
// OLED HELPERS
// ==========================================
void showStandby() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(20, 0);  display.println("GuardianPod v1.0");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setCursor(10, 20); display.println("Status: STANDBY");
  display.setCursor(10, 36); display.println("Monitoring...");
  display.display();
}

void showMessage(String l1, String l2 = "", String l3 = "") {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 8);  display.println(l1);
  display.setCursor(0, 28); display.println(l2);
  display.setCursor(0, 48); display.println(l3);
  display.display();
}

void showPersonDetected(int count) {
  display.clearDisplay();
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.drawRect(2, 2, 124, 60, SSD1306_WHITE);
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(14, 8);  display.println("PERSON!");
  display.drawLine(4, 30, 124, 30, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(8, 36);  display.println("Sending alert...");
  display.setCursor(8, 50);  display.print("Alert #"); display.println(count);
  display.display();
}

void showSent(int count) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 10); display.println("Firebase OK!");
  display.setCursor(28, 28); display.println("Alert sent");
  display.setCursor(8,  46); display.print("Total alerts: "); display.println(count);
  display.display();
  delay(2000);
  showStandby();
}

// ==========================================
// CAMERA INIT
// ==========================================
bool initCamera() {
  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.pin_d0       = Y2_GPIO_NUM;
  cfg.pin_d1       = Y3_GPIO_NUM;
  cfg.pin_d2       = Y4_GPIO_NUM;
  cfg.pin_d3       = Y5_GPIO_NUM;
  cfg.pin_d4       = Y6_GPIO_NUM;
  cfg.pin_d5       = Y7_GPIO_NUM;
  cfg.pin_d6       = Y8_GPIO_NUM;
  cfg.pin_d7       = Y9_GPIO_NUM;
  cfg.pin_xclk     = XCLK_GPIO_NUM;
  cfg.pin_pclk     = PCLK_GPIO_NUM;
  cfg.pin_vsync    = VSYNC_GPIO_NUM;
  cfg.pin_href     = HREF_GPIO_NUM;
  cfg.pin_sccb_sda = SIOD_GPIO_NUM;
  cfg.pin_sccb_scl = SIOC_GPIO_NUM;
  cfg.pin_pwdn     = PWDN_GPIO_NUM;
  cfg.pin_reset    = RESET_GPIO_NUM;
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.frame_size   = FRAMESIZE_SVGA;   // 800x600 — better ML accuracy
  cfg.grab_mode    = CAMERA_GRAB_LATEST;
  cfg.jpeg_quality = 12;
  cfg.fb_count     = 2;
  cfg.fb_location  = CAMERA_FB_IN_PSRAM;

  if (esp_camera_init(&cfg) != ESP_OK) {
    Serial.println("[ERROR] Camera init failed");
    return false;
  }

  // Flip camera — ลองทีละ combination
  // 0,0 = ไม่ flip  |  1,0 = flip vertical
  // 0,1 = mirror    |  1,1 = flip both
  sensor_t* s = esp_camera_sensor_get();
  s->set_vflip(s, 0);
  s->set_hmirror(s, 1);

  Serial.println("[OK] Camera ready");
  return true;
}

// ==========================================
// BASE64 ENCODER
// ==========================================
static const char b64chars[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String toBase64(const uint8_t* data, size_t len) {
  String result = "";
  result.reserve((len / 3 + 1) * 4 + 4);
  int i = 0;
  uint8_t b3[3], b4[4];
  while (len--) {
    b3[i++] = *data++;
    if (i == 3) {
      b4[0] = (b3[0] & 0xfc) >> 2;
      b4[1] = ((b3[0] & 0x03) << 4) + ((b3[1] & 0xf0) >> 4);
      b4[2] = ((b3[1] & 0x0f) << 2) + ((b3[2] & 0xc0) >> 6);
      b4[3] = b3[2] & 0x3f;
      for (i = 0; i < 4; i++) result += b64chars[b4[i]];
      i = 0;
    }
  }
  if (i) {
    for (int j = i; j < 3; j++) b3[j] = 0;
    b4[0] = (b3[0] & 0xfc) >> 2;
    b4[1] = ((b3[0] & 0x03) << 4) + ((b3[1] & 0xf0) >> 4);
    b4[2] = ((b3[1] & 0x0f) << 2) + ((b3[2] & 0xc0) >> 6);
    for (int j = 0; j < i + 1; j++) result += b64chars[b4[j]];
    while (i++ < 3) result += '=';
  }
  return result;
}

// ==========================================
// FIREBASE — ส่ง alert + รูป
// ==========================================
void sendAlertToFirebase(int count, camera_fb_t* fb) {
  if (!firebaseReady) {
    Serial.println("[WARN] Firebase not ready");
    return;
  }

  String imgBase64 = "";
  if (fb) {
    Serial.println("[Base64] Encoding...");
    showMessage("Encoding...", "image to Base64", "");
    imgBase64 = toBase64(fb->buf, fb->len);
    Serial.printf("[Base64] Done — %d chars\n", imgBase64.length());
  }

  String path = "/keepmesafe/" + String(ROOM_ID) +
                "/alerts/alert_" + String(count);

  String jsonStr = "{";
  jsonStr += "\"status\":\"ALERT\",";
  jsonStr += "\"message\":\"Person detected in room\",";
  jsonStr += "\"alert_num\":" + String(count) + ",";
  jsonStr += "\"timestamp\":" + String(millis()) + ",";
  jsonStr += "\"image\":\"" + imgBase64 + "\"";
  jsonStr += "}";

  Serial.printf("[Firebase] Sending %d bytes...\n", jsonStr.length());
  showMessage("Sending...", String(jsonStr.length()) + " bytes", "");

  object_t json(jsonStr.c_str());

  Database.set<object_t>(aClient, path, json, [](AsyncResult &aResult) {
    if (aResult.isError()) {
      Serial.println("[Firebase] Error: " + aResult.error().message());
    } else {
      Serial.println("[Firebase] Alert + Image sent OK");
    }
  });

  String latestPath = "/keepmesafe/" + String(ROOM_ID) + "/alerts/latest";
  Database.set<object_t>(aClient, latestPath, json, [](AsyncResult &r) {});

  // Free memory to prevent leak
  imgBase64 = "";
  imgBase64.clear();
  jsonStr = "";
  jsonStr.clear();
}

// ==========================================
// ML INFERENCE — Edge Impulse (same as Yan's working code)
// ==========================================
static uint8_t* rgb_buffer    = nullptr;
static uint8_t* resized_buffer = nullptr;

float runMLInference(camera_fb_t* fb) {
  if (!fb) return 0.0f;

  Serial.printf("[ML] PSRAM free: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  // Alloc buffers once, reuse across calls
  size_t svga_size    = 800 * 600 * 3;
  size_t resized_size = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * 3;

  if (rgb_buffer == nullptr) {
    rgb_buffer = (uint8_t*)ps_malloc(svga_size);
  }
  if (rgb_buffer == nullptr) {
    Serial.println("[ML] ERR:MALLOC rgb_buffer");
    return 0.0f;
  }

  if (resized_buffer == nullptr) {
    resized_buffer = (uint8_t*)ps_malloc(resized_size);
  }
  if (resized_buffer == nullptr) {
    Serial.println("[ML] ERR:MALLOC2 resized_buffer");
    return 0.0f;
  }

  // Convert JPEG → RGB888
  bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb_buffer);
  if (!converted) {
    Serial.println("[ML] ERR:CONVERT");
    return 0.0f;
  }

  // Resize 800x600 → 96x96
  int src_w = 800, src_h = 600;
  int dst_w = EI_CLASSIFIER_INPUT_WIDTH;
  int dst_h = EI_CLASSIFIER_INPUT_HEIGHT;
  for (int y = 0; y < dst_h; y++) {
    for (int x = 0; x < dst_w; x++) {
      int sx      = x * src_w / dst_w;
      int sy      = y * src_h / dst_h;
      int src_idx = (sy * src_w + sx) * 3;
      int dst_idx = (y  * dst_w + x)  * 3;
      resized_buffer[dst_idx]     = rgb_buffer[src_idx];
      resized_buffer[dst_idx + 1] = rgb_buffer[src_idx + 1];
      resized_buffer[dst_idx + 2] = rgb_buffer[src_idx + 2];
    }
  }

  // Edge Impulse signal — exact copy of Yan's lambda
  ei::signal_t signal;
  signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  signal.get_data = [](size_t offset, size_t length, float *out_ptr) -> int {
    for (size_t i = 0; i < length; i++) {
      size_t pixel_idx = (offset + i) * 3;
      uint8_t r = resized_buffer[pixel_idx];
      uint8_t g = resized_buffer[pixel_idx + 1];
      uint8_t b = resized_buffer[pixel_idx + 2];
      out_ptr[i] = (float)((b << 16) | (g << 8) | b);  // Yan's exact formula
    }
    return 0;
  };

  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
  if (err != EI_IMPULSE_OK) {
    Serial.printf("[ML] Classifier error: %d\n", err);
    return 0.0f;
  }

  Serial.println("[ML] Results:");
  float personConfidence = 0.0f;
  for (uint8_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    Serial.printf("  %s: %.2f\n",
      result.classification[ix].label,
      result.classification[ix].value);
    if (String(result.classification[ix].label) == "person") {
      personConfidence = result.classification[ix].value;
    }
  }
  return personConfidence;
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);

  esp_task_wdt_deinit();

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(D1, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  xTaskCreate(blinkTask, "blink", 4096, NULL, 1, &blinkTaskHandle);

  Wire.begin(D4, D5);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[ERROR] OLED not found");
    while (true);
  }
  showMessage("GuardianPod", "Starting...");
  delay(500);

  // LOGO SCREEN
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(18, 8);  display.println("KEEP ME SAFE");
  display.drawLine(2, 20, 125, 20, SSD1306_WHITE);
  display.drawCircle(64, 36, 8, SSD1306_WHITE);
  display.fillRect(58, 40, 12, 10, SSD1306_WHITE);
  display.fillCircle(64, 36, 5, SSD1306_BLACK);
  display.setCursor(30, 54);  display.println("v1.0  GuardianPod");
  display.display();
  delay(2500);

  // WiFi prompt
  display.clearDisplay();
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(28, 8);  display.println("Connect WiFi?");
  display.drawLine(2, 20, 125, 20, SSD1306_WHITE);
  display.setCursor(8, 28);  display.println("Press D1 to connect");
  display.setCursor(8, 42);  display.println("Wait 10s to skip");
  display.drawRect(4, 56, 120, 6, SSD1306_WHITE);
  display.display();

  bool connectWifi = false;
  unsigned long waitStart = millis();
  while (millis() - waitStart < 10000) {
    if (digitalRead(D1) == LOW) {
      delay(50);
      if (digitalRead(D1) == LOW) { connectWifi = true; break; }
    }
    int barWidth = map(millis() - waitStart, 0, 10000, 0, 116);
    display.fillRect(6, 58, barWidth, 2, SSD1306_WHITE);
    display.display();
    delay(100);
  }

  if (connectWifi) {
    showMessage("Connecting WiFi", "Please wait...", "");
  } else {
    showMessage("Skipped WiFi", "Using saved WiFi", "");
    delay(1000);
  }

  WiFiManager wm;
  if (connectWifi) {
    showMessage("WiFi Portal", "Connect phone to:", "KeepMeSafe");
    wm.resetSettings();
  }
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect("KeepMeSafe", "12345678")) {
    Serial.println("[ERROR] WiFi failed");
    ESP.restart();
  }
  Serial.println("[OK] WiFi: " + WiFi.SSID());
  showMessage("WiFi OK!", WiFi.SSID(), WiFi.localIP().toString());
  delay(1500);

  showMessage("Camera", "Initializing...");
  if (!initCamera()) {
    showMessage("Camera ERROR", "Check wiring");
    while (true);
  }

  showMessage("Firebase", "Connecting...");
  ssl.setInsecure();
  initializeApp(aClient, app, getAuth(noAuth));
  app.getApp<RealtimeDatabase>(Database);
  Database.url(FIREBASE_DATABASE_URL);

  unsigned long ms = millis();
  while (!app.ready() && millis() - ms < 8000) {
    app.loop();
    delay(100);
  }

  firebaseReady = app.ready();
  if (firebaseReady) {
    Serial.println("[OK] Firebase ready");
    showMessage("Firebase OK!", "System Ready", "Monitoring...");
  } else {
    Serial.println("[WARN] Firebase not ready yet");
    showMessage("Firebase WARN", "Will retry...");
  }

  delay(1500);
  showStandby();
  Serial.println("[OK] Ready — PIR on D9");
}

// ==========================================
// LOOP
// ==========================================
void loop() {
  app.loop();

  if (!firebaseReady && app.ready()) {
    firebaseReady = true;
    Serial.println("[OK] Firebase ready (retry)");
  }

  // กด D1 ค้าง 2 วินาที → reset WiFi
  if (digitalRead(D1) == LOW) {
    unsigned long pressStart = millis();
    while (digitalRead(D1) == LOW) {
      if (millis() - pressStart >= 2000) {
        Serial.println("[BTN] WiFi reset!");
        showMessage("WiFi Reset!", "Opening portal...", "KeepMeSafe");
        alarmLED   = false;
        playMelody = false;

        WiFiManager wm;
        wm.resetSettings();
        wm.setConfigPortalTimeout(180);
        wm.startConfigPortal("KeepMeSafe", "12345678");

        Serial.println("[OK] WiFi: " + WiFi.SSID());
        showMessage("WiFi OK!", WiFi.SSID(), WiFi.localIP().toString());
        delay(1500);

        ssl.setInsecure();
        initializeApp(aClient, app, getAuth(noAuth));
        app.getApp<RealtimeDatabase>(Database);
        Database.url(FIREBASE_DATABASE_URL);

        unsigned long ms = millis();
        while (!app.ready() && millis() - ms < 8000) {
          app.loop();
          delay(100);
        }
        firebaseReady = app.ready();
        Serial.println(firebaseReady ? "[OK] Firebase ready" : "[WARN] Firebase not ready");
        showStandby();
        break;
      }
      delay(50);
    }
  }

  bool pirState = digitalRead(PIR_PIN);

  if (pirState == HIGH &&
      millis() - lastCaptureTime >= CAPTURE_COOLDOWN) {

    alertCount++;
    lastCaptureTime = millis();
    alarmLED = true;
    Serial.println("[PIR] Motion! Alert #" + String(alertCount));
    showPersonDetected(alertCount);

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[ERROR] Capture failed");
      showMessage("Camera ERROR", "Capture failed");
      alarmLED = false;
    } else {
      Serial.printf("[Camera] %d bytes\n", fb->len);

      showMessage("ML Running...", "Person detect", "Please wait");

      // 3-frame majority vote — ต้องเจอคน 2/3 ครั้ง
      int personVotes = 0;
      for (int v = 0; v < 3; v++) {
        float conf = runMLInference(fb);
        if (conf >= EI_CLASSIFIER_THRESHOLD) personVotes++;
        delay(200);
      }
      float confidence = (personVotes >= 2) ? 1.0f : 0.0f;
      Serial.printf("[ML] Votes: %d/3 → %s\n", personVotes,
        personVotes >= 2 ? "PERSON" : "no person");

      if (confidence >= EI_CLASSIFIER_THRESHOLD) {
        Serial.println("[ML] Person detected! Sending alert...");
        showPersonDetected(alertCount);
        sendAlertToFirebase(alertCount, fb);
        esp_camera_fb_return(fb);
        showSent(alertCount);
        alarmLED   = false;
        playMelody = true;
      } else {
        Serial.printf("[ML] Not a person (%.2f) — skipping\n", confidence);
        showMessage("ML: No Person", String(confidence * 100, 0) + "% confidence", "Ignored");
        esp_camera_fb_return(fb);
        alarmLED = false;
        delay(1500);
        showStandby();
      }
    }
  }

  if (pirState == LOW && lastPIRState == HIGH) {
    alarmLED = false;
  }

  lastPIRState = pirState;
  delay(50);
}