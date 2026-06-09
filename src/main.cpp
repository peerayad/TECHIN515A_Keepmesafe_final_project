#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FirebaseClient.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "esp_camera.h"
#include "esp_task_wdt.h"
#include <ML_Camera_inferencing.h>

#define PERSON_THRESHOLD 0.5f

// ==========================================
// PIN DEFINITIONS
// ==========================================
#define PIR_PIN    D0
#define BUZZER_PIN D3
#define BTN_OK     D6   // กด = ต่อ WiFi
#define BTN_CANCEL D7   // กด = offline SD mode
#define SD_CS      21

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

// ==========================================
// FORWARD DECLARATIONS
// ==========================================
void buzzShort();
void playAlarm();
void showStandby();
void showMessage(String l1, String l2 = "", String l3 = "");
void showResult(bool isPerson, float confidence, int votes);
void showPersonDetected(int count);
void showSent(int count);
void drawWifiIcon(bool connected);
bool initCamera();
bool initSD();
float runMLInference(camera_fb_t* fb);
void sendAlertToFirebase(int count, camera_fb_t* fb);
void saveToSD(int count, camera_fb_t* fb);
String toBase64(const uint8_t* data, size_t len);
void openSettingsMenu();
bool rotateRGB90CCW(const uint8_t* src, int src_w, int src_h,
                    uint8_t** out_jpg, size_t* out_len);

// ==========================================
// OLED INSTANCE
// ==========================================
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
// CAMERA PINS
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
// MISSION IMPOSSIBLE THEME
// ==========================================
#define NOTE_B3  247
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_CS5 554
#define NOTE_D5  587
#define NOTE_DS5 622
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_FS5 740
#define NOTE_G5  784
#define NOTE_GS5 831
#define NOTE_A5  880
#define REST     0

// River Flows in You — Yiruma
int riverMelody[] = {
  // Phrase 1
  NOTE_E5,  NOTE_CS5, NOTE_A4,  NOTE_B4,  NOTE_CS5, NOTE_A4,
  NOTE_B4,  NOTE_CS5, NOTE_D5,  NOTE_CS5, NOTE_B4,
  NOTE_A4,  NOTE_CS5, NOTE_E5,  NOTE_A5,
  NOTE_GS5, NOTE_FS5, NOTE_E5,  NOTE_D5,  NOTE_CS5,
  // Phrase 2
  NOTE_E5,  NOTE_CS5, NOTE_A4,  NOTE_B4,  NOTE_CS5, NOTE_A4,
  NOTE_B4,  NOTE_CS5, NOTE_D5,  NOTE_CS5, NOTE_B4,
  NOTE_A4,  NOTE_B4,  NOTE_CS5, NOTE_D5,  NOTE_E5,
  NOTE_A4,
  // Phrase 3
  NOTE_E5,  NOTE_CS5, NOTE_A4,  NOTE_B4,  NOTE_CS5, NOTE_A4,
  NOTE_B4,  NOTE_CS5, NOTE_D5,  NOTE_CS5, NOTE_B4,
  NOTE_A4,  NOTE_CS5, NOTE_E5,  NOTE_A5,
  NOTE_GS5, NOTE_FS5, NOTE_E5,  NOTE_D5,  NOTE_CS5,
  NOTE_B4,  NOTE_A4
};

int riverDurations[] = {
  // Phrase 1
  4, 8, 8, 8, 8, 4,
  4, 8, 8, 4, 4,
  4, 4, 4, 4,
  8, 8, 4, 4, 4,
  // Phrase 2
  4, 8, 8, 8, 8, 4,
  4, 8, 8, 4, 4,
  4, 8, 8, 4, 4,
  2,
  // Phrase 3
  4, 8, 8, 8, 8, 4,
  4, 8, 8, 4, 4,
  4, 4, 4, 4,
  8, 8, 4, 4, 4,
  4, 1
};

int totalNotes = sizeof(riverMelody) / sizeof(riverMelody[0]);

bool soundEnabled = true;  // ประกาศก่อน playAlarm/buzzShort

void playAlarm() {
  // แสดง ALARM! บน OLED
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(22, 8);  display.println("ALARM!");
  display.drawLine(2, 30, 125, 30, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(8, 38);  display.println("Intruder detected!");
  display.setCursor(14, 52); display.println("B: Cancel alarm");
  display.display();

  if (!soundEnabled) {
    // ไม่มีเสียง แต่รอให้กด B เพื่อกลับ standby
    while (digitalRead(BTN_CANCEL) != LOW) delay(50);
    showStandby();
    return;
  }

  for (int i = 0; i < totalNotes; i++) {
    int noteDuration = 1000 / riverDurations[i];
    if (riverMelody[i] == REST) {
      noTone(BUZZER_PIN);
    } else {
      tone(BUZZER_PIN, riverMelody[i], noteDuration);
    }
    unsigned long t = millis();
    while (millis() - t < (unsigned long)(noteDuration * 1.3)) {
      if (digitalRead(BTN_CANCEL) == LOW) {
        noTone(BUZZER_PIN);
        showStandby();
        return;  // กด B → cancel → standby
      }
      delay(10);
    }
    noTone(BUZZER_PIN);
  }
  // เพลงจบเอง → กลับ standby
  showStandby();
}

void buzzShort() {
  if (!soundEnabled) return;
  tone(BUZZER_PIN, 1000, 100);
  delay(150);
  noTone(BUZZER_PIN);
}

// ==========================================
// STATE
// ==========================================
bool          lastPIRState    = false;
int           alertCount      = 0;
bool          firebaseReady   = false;
bool          sdReady         = false;
bool          wifiConnected   = false;
unsigned long lastCaptureTime = 0;
#define CAPTURE_COOLDOWN 10000

// ==========================================
// WIFI ICON (มุมขวาบน)
// ==========================================
void drawWifiIcon(bool connected) {
  // วาด WiFi icon ที่ x=112 y=0 ขนาด 14x8
  if (connected) {
    // arc ใหญ่
    display.drawCircle(119, 8, 7, SSD1306_WHITE);
    // arc กลาง
    display.drawCircle(119, 8, 4, SSD1306_WHITE);
    // จุดตรงกลาง
    display.fillCircle(119, 8, 1, SSD1306_WHITE);
    // ลบครึ่งล่าง arc ออก ให้เหลือแค่ครึ่งบน
    display.fillRect(112, 9, 16, 8, SSD1306_BLACK);
  } else {
    // X แทน WiFi ตอน offline
    display.drawLine(113, 1, 125, 7, SSD1306_WHITE);
    display.drawLine(125, 1, 113, 7, SSD1306_WHITE);
  }
}

// ==========================================
// OLED HELPERS
// ==========================================
void showStandby() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);   display.println("GuardianPod v1.0");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setCursor(10, 20); display.println("Status: STANDBY");
  display.setCursor(10, 36); display.println("Monitoring...");
  display.setCursor(10, 50);
  if (wifiConnected) {
    display.println("Mode: Firebase");
  } else {
    display.println("Mode: SD Card");
  }
  drawWifiIcon(wifiConnected);
  display.display();
}

void showMessage(String l1, String l2, String l3) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 8);  display.println(l1);
  display.setCursor(0, 28); display.println(l2);
  display.setCursor(0, 48); display.println(l3);
  drawWifiIcon(wifiConnected);
  display.display();
}

void showResult(bool isPerson, float confidence, int votes) {
  display.clearDisplay();
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  if (isPerson) {
    display.setTextSize(2);
    display.setCursor(14, 6); display.println("INTRUDER!");
  } else {
    display.setTextSize(2);
    display.setCursor(4, 6);  display.println("NO PERSON");
  }
  display.drawLine(2, 28, 126, 28, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(8, 34);
  display.print("Conf: ");
  display.print(confidence * 100, 0);
  display.println("%");
  display.setCursor(8, 46);
  display.print("Votes: ");
  display.print(votes);
  display.println("/3");
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
  if (wifiConnected) {
    display.setCursor(8, 36); display.println("Sending Firebase");
  } else {
    display.setCursor(8, 36); display.println("Saving to SD");
  }
  display.setCursor(8, 50); display.print("Alert #"); display.println(count);
  drawWifiIcon(wifiConnected);
  display.display();
}

void showSent(int count) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  if (wifiConnected) {
    display.setCursor(20, 10); display.println("Firebase OK!");
    display.setCursor(28, 28); display.println("Alert sent");
  } else {
    display.setCursor(20, 10); display.println("SD Card OK!");
    display.setCursor(20, 28); display.println("Image saved");
  }
  display.setCursor(8, 46); display.print("Total alerts: "); display.println(count);
  drawWifiIcon(wifiConnected);
  display.display();
  delay(2000);
  showStandby();
}

// ==========================================
// CAMERA INIT
// ==========================================
bool initCamera() {
  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_1;
  cfg.ledc_timer   = LEDC_TIMER_1;
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
  cfg.frame_size   = FRAMESIZE_SVGA;
  cfg.grab_mode    = CAMERA_GRAB_LATEST;
  cfg.jpeg_quality = 12;
  cfg.fb_count     = 2;
  cfg.fb_location  = CAMERA_FB_IN_PSRAM;

  if (esp_camera_init(&cfg) != ESP_OK) {
    Serial.println("[ERROR] Camera init failed");
    return false;
  }
  sensor_t* s = esp_camera_sensor_get();
  s->set_vflip(s, 0);    // RAW — ไม่ flip
  s->set_hmirror(s, 0);  // RAW — ไม่ mirror
  Serial.println("[OK] Camera ready");
  return true;
}

// ==========================================
// SD CARD INIT
// ==========================================
bool initSD() {
  if (!SD.begin(SD_CS)) {
    Serial.println("[ERROR] SD card mount failed");
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    Serial.println("[ERROR] No SD card");
    return false;
  }
  if (!SD.exists("/alerts")) SD.mkdir("/alerts");
  Serial.println("[OK] SD card ready");
  return true;
}

// ==========================================
// SAVE TO SD
// ==========================================
void saveToSD(int count, camera_fb_t* fb) {
  if (!sdReady || !fb) return;
  String filename = "/alerts/alert_" + String(count) + ".jpg";
  Serial.printf("[SD] Saving %s...\n", filename.c_str());
  showMessage("Saving to SD...", filename, "");
  File file = SD.open(filename, FILE_WRITE);
  if (!file) { Serial.println("[ERROR] Failed to open file"); return; }
  file.write(fb->buf, fb->len);
  file.close();
  Serial.printf("[SD] Saved %d bytes\n", fb->len);
  File log = SD.open("/alerts/log.txt", FILE_APPEND);
  if (log) {
    log.printf("alert_%d.jpg | %lu ms | %d bytes\n", count, millis(), fb->len);
    log.close();
  }
}

// ==========================================
// BASE64
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
// JPEG ROTATION (90° CCW) — reuse rgb_buffer from ML
// ==========================================
// หมายเหตุ: เรียกหลัง runMLInference() เท่านั้น (rgb_buffer ต้องมีข้อมูลแล้ว)
bool rotateRGB90CCW(const uint8_t* src, int src_w, int src_h,
                    uint8_t** out_jpg, size_t* out_len) {
  int nw = src_h, nh = src_w;  // 90° CCW: new_w=old_h, new_h=old_w
  size_t rgb_size = (size_t)src_w * src_h * 3;

  uint8_t* rot = (uint8_t*)heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM);
  if (!rot) { Serial.println("[ROT] malloc failed"); return false; }

  // CCW: new[row][col] = old[col][old_w - 1 - row]
  for (int row = 0; row < nh; row++) {
    for (int col = 0; col < nw; col++) {
      uint8_t* d = rot + ((size_t)row * nw + col) * 3;
      uint8_t* s = (uint8_t*)src + ((size_t)col * src_w + (src_w - 1 - row)) * 3;
      d[0]=s[0]; d[1]=s[1]; d[2]=s[2];
    }
  }

  *out_jpg = nullptr; *out_len = 0;
  bool ok = fmt2jpg(rot, rgb_size, nw, nh, PIXFORMAT_RGB888, 80, out_jpg, out_len);
  heap_caps_free(rot);
  return ok;
}

// ==========================================
// FIREBASE SEND
// ==========================================
void sendAlertToFirebase(int count, camera_fb_t* fb) {
  if (!firebaseReady) { Serial.println("[WARN] Firebase not ready"); return; }

  String imgBase64 = "";
  if (fb) {
    showMessage("Encoding...", "image to Base64", "");
    imgBase64 = toBase64(fb->buf, fb->len);
    Serial.printf("[Base64] Done — %d chars\n", imgBase64.length());
  }

  String path = "/keepmesafe/" + String(ROOM_ID) + "/alerts/alert_" + String(count);
  String jsonStr = "{";
  jsonStr += "\"status\":\"ALERT\",";
  jsonStr += "\"message\":\"Person detected in room\",";
  jsonStr += "\"alert_num\":" + String(count) + ",";
  jsonStr += "\"timestamp\":{\".sv\":\"timestamp\"},";  // Firebase Server Timestamp
  jsonStr += "\"image\":\"" + imgBase64 + "\"";
  jsonStr += "}";

  showMessage("Sending...", String(jsonStr.length()) + " bytes", "");
  object_t json(jsonStr.c_str());
  Database.set<object_t>(aClient, path, json, [](AsyncResult &aResult) {
    if (aResult.isError()) Serial.println("[Firebase] Error: " + aResult.error().message());
    else Serial.println("[Firebase] Sent OK");
  });
  String latestPath = "/keepmesafe/" + String(ROOM_ID) + "/alerts/latest";
  Database.set<object_t>(aClient, latestPath, json, [](AsyncResult &r) {});
  imgBase64 = ""; jsonStr = "";
}

// ==========================================
// ML INFERENCE
// ==========================================
static uint8_t* rgb_buffer     = nullptr;
static uint8_t* rot_buffer     = nullptr;  // buffer หลัง rotate 90° CCW
static uint8_t* resized_buffer = nullptr;

float runMLInference(camera_fb_t* fb) {
  if (!fb) return 0.0f;

  size_t svga_size    = 800 * 600 * 3;
  size_t resized_size = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * 3;

  if (rgb_buffer == nullptr)
    rgb_buffer = (uint8_t*)heap_caps_malloc(svga_size, MALLOC_CAP_SPIRAM);
  if (rgb_buffer == nullptr) { Serial.println("[ML] ERR:MALLOC rgb"); return 0.0f; }

  if (rot_buffer == nullptr)
    rot_buffer = (uint8_t*)heap_caps_malloc(svga_size, MALLOC_CAP_SPIRAM);
  if (rot_buffer == nullptr) { Serial.println("[ML] ERR:MALLOC rot"); return 0.0f; }

  if (resized_buffer == nullptr)
    resized_buffer = (uint8_t*)heap_caps_malloc(resized_size, MALLOC_CAP_SPIRAM);
  if (resized_buffer == nullptr) { Serial.println("[ML] ERR:MALLOC resized"); return 0.0f; }

  // Decode JPEG → RGB888 (landscape 800×600)
  if (!fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb_buffer)) {
    Serial.println("[ML] ERR:CONVERT"); return 0.0f;
  }

  // Rotate 90° CCW → portrait (600×800) ให้ตรงกับ training data
  for (int row = 0; row < 800; row++) {       // new_h = old_w = 800
    for (int col = 0; col < 600; col++) {     // new_w = old_h = 600
      uint8_t* d = rot_buffer + (row * 600 + col) * 3;
      uint8_t* s = rgb_buffer + (col * 800 + (800 - 1 - row)) * 3;
      d[0]=s[0]; d[1]=s[1]; d[2]=s[2];
    }
  }

  // Resize จาก portrait (600×800) → EI input size
  int src_w = 600, src_h = 800;
  int dst_w = EI_CLASSIFIER_INPUT_WIDTH;
  int dst_h = EI_CLASSIFIER_INPUT_HEIGHT;
  for (int y = 0; y < dst_h; y++) {
    for (int x = 0; x < dst_w; x++) {
      int sx = x * src_w / dst_w, sy = y * src_h / dst_h;
      int si = (sy * src_w + sx) * 3, di = (y * dst_w + x) * 3;
      resized_buffer[di]   = rot_buffer[si];
      resized_buffer[di+1] = rot_buffer[si+1];
      resized_buffer[di+2] = rot_buffer[si+2];
    }
  }

  // ==========================================
  // PREPROCESSING: Auto-contrast + Brightness
  // ทำบน resized_buffer (เล็ก เร็ว) ก่อนส่งเข้า classifier
  // ==========================================
  int total_px = dst_w * dst_h;

  // 1. Auto-contrast per channel: stretch min-max → 0-255
  for (int c = 0; c < 3; c++) {
    uint8_t minV = 255, maxV = 0;
    for (int i = 0; i < total_px; i++) {
      uint8_t v = resized_buffer[i * 3 + c];
      if (v < minV) minV = v;
      if (v > maxV) maxV = v;
    }
    if (maxV > minV) {
      float scale = 255.0f / (maxV - minV);
      for (int i = 0; i < total_px; i++) {
        int idx = i * 3 + c;
        resized_buffer[idx] = (uint8_t)((resized_buffer[idx] - minV) * scale);
      }
    }
  }

  // 2. Contrast boost: alpha=1.2 (ดึง contrast ออก), beta=0 (brightness คงเดิม)
  const float alpha = 1.0f;
  for (int i = 0; i < total_px * 3; i++) {
    float val = alpha * resized_buffer[i];
    resized_buffer[i] = (uint8_t)(val > 255 ? 255 : val);
  }

  ei::signal_t signal;
  signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  signal.get_data = [](size_t offset, size_t length, float *out_ptr) -> int {
    for (size_t i = 0; i < length; i++) {
      size_t pi = (offset + i) * 3;
      out_ptr[i] = (float)((resized_buffer[pi] << 16) |
                           (resized_buffer[pi+1] << 8) |
                            resized_buffer[pi+2]);
    }
    return 0;
  };

  ei_impulse_result_t result = { 0 };
  if (run_classifier(&signal, &result, false) != EI_IMPULSE_OK) return 0.0f;

  float personConfidence = 0.0f;
  for (uint8_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    Serial.printf("  %s: %.2f\n", result.classification[ix].label, result.classification[ix].value);
    if (String(result.classification[ix].label) == "person")
      personConfidence = result.classification[ix].value;
  }
  return personConfidence;
}

// ==========================================
// SETTINGS MENU
// ==========================================
void showMainMenu(int cursor) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(34, 6);  display.println("SETTINGS");
  display.drawLine(2, 16, 125, 16, SSD1306_WHITE);
  display.setCursor(4, 24);  display.print(cursor == 0 ? "> " : "  ");  display.println("Mode");
  display.setCursor(4, 38);  display.print(cursor == 1 ? "> " : "  ");  display.println("Sound");
  display.drawLine(2, 52, 125, 52, SSD1306_WHITE);
  display.setCursor(2, 55);  display.println("A:Select  B:Next/Exit");
  display.display();
}

void openModeMenu() {
  while (true) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(40, 6);  display.println("MODE");
    display.drawLine(2, 16, 125, 16, SSD1306_WHITE);
    display.setCursor(4, 24);  display.print("Now: ");
    display.println(wifiConnected ? "WIFI" : "SD CARD");
    display.setCursor(4, 38);  display.print("To : ");
    display.println(wifiConnected ? "SD CARD" : "WIFI");
    display.drawLine(2, 52, 125, 52, SSD1306_WHITE);
    display.setCursor(2, 55);  display.println("A:Change   B:Back");
    display.display();

    bool acted = false;
    while (!acted) {
      if (digitalRead(BTN_OK) == LOW) {
        delay(50);
        if (digitalRead(BTN_OK) == LOW) {
          if (!wifiConnected) {
            // ลอง saved WiFi ก่อน
            showMessage("Connecting WiFi", "Trying saved...", "");
            WiFi.begin();
            unsigned long t = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) delay(500);

            if (WiFi.status() == WL_CONNECTED) {
              wifiConnected = true;
              showMessage("WiFi OK!", WiFi.SSID(), "");
              ssl.setInsecure();
              initializeApp(aClient, app, getAuth(noAuth));
              app.getApp<RealtimeDatabase>(Database);
              Database.url(FIREBASE_DATABASE_URL);
              unsigned long ms = millis();
              while (!app.ready() && millis() - ms < 8000) { app.loop(); delay(100); }
              firebaseReady = app.ready();
              delay(1500);
            } else {
              // ต่อไม่ได้ → WiFiManager
              showMessage("WiFi Failed", "Open portal...", "KeepMeSafe AP");
              WiFiManager wm;
              wm.setConfigPortalTimeout(180);
              if (wm.startConfigPortal("KeepMeSafe", "12345678")) {
                wifiConnected = true;
                showMessage("WiFi OK!", WiFi.SSID(), "");
                ssl.setInsecure();
                initializeApp(aClient, app, getAuth(noAuth));
                app.getApp<RealtimeDatabase>(Database);
                Database.url(FIREBASE_DATABASE_URL);
                unsigned long ms = millis();
                while (!app.ready() && millis() - ms < 8000) { app.loop(); delay(100); }
                firebaseReady = app.ready();
                delay(1500);
              } else {
                showMessage("WiFi Failed", "Stayed SD mode", "");
                delay(1500);
              }
            }
          } else {
            // Switch to SD Card
            WiFi.disconnect(true);
            wifiConnected  = false;
            firebaseReady  = false;
            showMessage("SD Card Mode", "WiFi off", "");
            delay(1500);
          }
          acted = true;
        }
      }
      if (digitalRead(BTN_CANCEL) == LOW) {
        delay(50);
        if (digitalRead(BTN_CANCEL) == LOW) return;  // B = กลับ
      }
      delay(50);
    }
  }
}

void openSoundMenu() {
  while (true) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(34, 6);  display.println("SOUND");
    display.drawLine(2, 16, 125, 16, SSD1306_WHITE);
    display.setCursor(4, 28);  display.print("Sound: ");
    display.setTextSize(2);
    display.println(soundEnabled ? "ON" : "OFF");
    display.setTextSize(1);
    display.drawLine(2, 52, 125, 52, SSD1306_WHITE);
    display.setCursor(2, 55);  display.println("A:Toggle   B:Back");
    display.display();

    while (true) {
      if (digitalRead(BTN_OK) == LOW) {
        delay(50);
        if (digitalRead(BTN_OK) == LOW) {
          soundEnabled = !soundEnabled;
          if (soundEnabled) { tone(BUZZER_PIN, 1000, 100); delay(150); noTone(BUZZER_PIN); }
          break;  // re-render
        }
      }
      if (digitalRead(BTN_CANCEL) == LOW) {
        delay(50);
        if (digitalRead(BTN_CANCEL) == LOW) return;  // B = กลับ
      }
      delay(50);
    }
  }
}

void openSettingsMenu() {
  int cursor = 0;
  showMainMenu(cursor);

  while (true) {
    if (digitalRead(BTN_OK) == LOW) {       // A = Select
      delay(50);
      if (digitalRead(BTN_OK) == LOW) {
        if (cursor == 0) openModeMenu();
        else             openSoundMenu();
        showMainMenu(cursor);
      }
    }
    if (digitalRead(BTN_CANCEL) == LOW) {   // B = Next item / Exit
      delay(50);
      if (digitalRead(BTN_CANCEL) == LOW) {
        cursor++;
        if (cursor > 1) return;             // ออกจากเมนู
        showMainMenu(cursor);
      }
    }
    delay(50);
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);
  esp_task_wdt_deinit();

  pinMode(PIR_PIN,    INPUT_PULLDOWN);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BTN_OK,     INPUT_PULLUP);
  pinMode(BTN_CANCEL, INPUT_PULLUP);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(D4, D5);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[ERROR] OLED not found");
    while (true);
  }
  display.setRotation(2);  // หมุน 180° (กลับด้าน)

  // LOGO
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

  // Camera ก่อนเสมอ (หลีก LEDC conflict)
  showMessage("Camera", "Initializing...", "");
  if (!initCamera()) {
    showMessage("Camera ERROR", "Check wiring", "");
    while (true);
  }

  // SD card
  showMessage("SD Card", "Initializing...", "");
  sdReady = initSD();
  showMessage(sdReady ? "SD Card OK!" : "SD WARN", sdReady ? "" : "Not found", "");
  delay(1000);

  // ถามว่าจะต่อ WiFi ไหม
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(22, 6);  display.println("Connect WiFi?");
  display.drawLine(2, 16, 125, 16, SSD1306_WHITE);
  display.setCursor(4, 22);  display.println("A: Connect WiFi");
  display.setCursor(4, 34);  display.println("B: SD Card mode");
  display.setCursor(4, 50);  display.println("Waiting...");
  display.display();

  // รอกด A หรือ B
  bool userChoice = false;  // false = SD mode, true = WiFi mode
  while (true) {
    if (digitalRead(BTN_OK) == LOW) {
      delay(50);
      if (digitalRead(BTN_OK) == LOW) { userChoice = true; break; }
    }
    if (digitalRead(BTN_CANCEL) == LOW) {
      delay(50);
      if (digitalRead(BTN_CANCEL) == LOW) { userChoice = false; break; }
    }
    delay(50);
  }

  if (userChoice) {
    // ---- WiFi mode ----
    showMessage("Connecting WiFi", "Please wait...", "");
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wm.setSaveConnectTimeout(30);

    if (wm.autoConnect("KeepMeSafe", "12345678")) {
      wifiConnected = true;
      Serial.println("[OK] WiFi: " + WiFi.SSID());
      showMessage("WiFi OK!", WiFi.SSID(), WiFi.localIP().toString());
      delay(1500);

      // Firebase
      showMessage("Firebase", "Connecting...", "");
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
      showMessage(firebaseReady ? "Firebase OK!" : "Firebase WARN",
                  firebaseReady ? "System Ready" : "Will retry...", "");
      delay(1500);
    } else {
      // WiFi ต่อไม่ได้ → fallback SD
      wifiConnected = false;
      Serial.println("[WARN] WiFi failed → SD mode");
      showMessage("WiFi Failed", "Using SD mode", "");
      delay(1500);
    }
  } else {
    // ---- SD mode ----
    wifiConnected = false;
    showMessage("SD Card Mode", "No WiFi", "Saving locally");
    delay(1500);
  }

  buzzShort();
  showStandby();
  Serial.println("[OK] Ready | WiFi: " + String(wifiConnected ? "ON" : "OFF") +
                 " | SD: " + String(sdReady ? "OK" : "NO"));
}

// ==========================================
// LOOP
// ==========================================
void loop() {
  // Check WiFi status — ถ้าหลุดให้ switch SD mode
  if (wifiConnected) {
    app.loop();

    if (WiFi.status() != WL_CONNECTED) {
      wifiConnected = false;
      firebaseReady = false;
      WiFi.disconnect(true);
      Serial.println("[WARN] WiFi lost → SD mode");
      showMessage("WiFi Lost!", "SD Card mode", "Monitoring...");
      delay(1500);
      showStandby();
    }

    if (!firebaseReady && app.ready()) {
      firebaseReady = true;
      Serial.println("[OK] Firebase ready (retry)");
    }
  }

  // A button → Settings menu
  if (digitalRead(BTN_OK) == LOW) {
    delay(50);
    if (digitalRead(BTN_OK) == LOW) {
      openSettingsMenu();
      showStandby();
    }
  }

  bool pirState = digitalRead(PIR_PIN);

  if (pirState == HIGH &&
      millis() - lastCaptureTime >= CAPTURE_COOLDOWN) {

    Serial.println("[PIR] Motion detected! Counting triggers for 3s...");
    showMessage("Motion!", "Confirming...", "Wait 3s");

    int triggerCount = 0;
    unsigned long startTime = millis();
    while (millis() - startTime < 3000) {
      if (digitalRead(PIR_PIN) == HIGH) triggerCount++;
      delay(200);
    }

    Serial.printf("[PIR] Trigger count: %d\n", triggerCount);

    if (triggerCount >= 3) {
      lastCaptureTime = millis();
      buzzShort();

      Serial.println("[PIR] Confirmed! Running ML...");
      showMessage("ML Running...", "Analyzing...", "Please wait");

      camera_fb_t* fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("[ERROR] Capture failed");
        showMessage("Camera ERROR", "Capture failed", "");
      } else {
        Serial.printf("[Camera] %d bytes\n", fb->len);

        int   personVotes     = 0;
        float totalConfidence = 0.0f;

        for (int v = 0; v < 3; v++) {
          camera_fb_t* vfb = esp_camera_fb_get();
          if (!vfb) {
            Serial.printf("[ML] Vote %d: capture failed\n", v + 1);
            // แสดงบน OLED
            display.clearDisplay();
            display.setTextSize(1);
            display.setTextColor(SSD1306_WHITE);
            display.setCursor(0, 0);  display.println("ML Voting...");
            display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
            display.setCursor(0, 16); display.printf("Vote %d/3", v + 1);
            display.setCursor(0, 30); display.println("Capture failed");
            display.display();
            continue;
          }
          float conf = runMLInference(vfb);
          esp_camera_fb_return(vfb);
          totalConfidence += conf;
          if (conf >= PERSON_THRESHOLD) personVotes++;

          // แสดงผล vote บน OLED เหมือน Serial
          Serial.printf("[ML] Vote %d: %.2f\n", v + 1, conf);
          display.clearDisplay();
          display.setTextSize(1);
          display.setTextColor(SSD1306_WHITE);
          display.setCursor(0, 0);  display.println("ML Voting...");
          display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
          display.setCursor(0, 16); display.printf("Vote %d/3", v + 1);
          display.setCursor(0, 28); display.printf("Person: %.0f%%", conf * 100);
          display.setCursor(0, 40); display.printf("Votes:  %d/3", personVotes);
          display.setCursor(0, 52); display.printf("Avg:    %.0f%%", (totalConfidence / (v + 1)) * 100);
          display.display();

          delay(100);
        }

        float avgConfidence = totalConfidence / 3.0f;
        bool isPerson = (personVotes >= 2);

        Serial.printf("[ML] Votes: %d/3 | Avg: %.2f → %s\n",
          personVotes, avgConfidence, isPerson ? "PERSON" : "NO PERSON");

        if (isPerson) {
          alertCount++;  // นับเฉพาะตอนที่ detect person จริงๆ
          showPersonDetected(alertCount);
          // Rotate 90° CCW โดยใช้ rgb_buffer ที่ ML decode ไว้แล้ว
          showMessage("Rotating...", "90 CCW", "");
          uint8_t* rotJpg = nullptr;
          size_t   rotLen = 0;
          bool rotOk = rotateRGB90CCW(rgb_buffer, 800, 600, &rotJpg, &rotLen);

          camera_fb_t rotFb;
          camera_fb_t* sendFb = fb;  // fallback
          if (rotOk && rotJpg) {
            rotFb.buf = rotJpg; rotFb.len = rotLen;
            rotFb.width = 600; rotFb.height = 800;
            rotFb.format = PIXFORMAT_JPEG;
            sendFb = &rotFb;
          }

          if (wifiConnected && firebaseReady) {
            Serial.println("[OK] Sending to Firebase");
            sendAlertToFirebase(alertCount, sendFb);
          } else {
            Serial.println("[OK] Saving to SD card");
            saveToSD(alertCount, sendFb);
          }
          if (rotJpg) free(rotJpg);
          lastCaptureTime = millis();  // reset cooldown หลังส่งเสร็จ ป้องกัน re-trigger
          showSent(alertCount);
          playAlarm();
        } else {
          showResult(isPerson, avgConfidence, personVotes);
          delay(3000);
          showStandby();
        }

        esp_camera_fb_return(fb);
      }
    } else {
      Serial.println("[PIR] False trigger — ignored");
      showStandby();
    }
  }

  lastPIRState = pirState;
  delay(50);
}