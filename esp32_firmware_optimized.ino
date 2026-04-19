/*
 * ESP32 Smart Trash Bin — Optimized Production Firmware
 *
 * Optimizations vs. esp32_firmware_fixed.ino:
 *   • F() macros on all string literals (keeps strings in flash, reduces heap pressure)
 *   • sendData() takes const char* (no String copy on every call)
 *   • JSON built with String.reserve(220) — stops 15+ heap reallocations per send
 *   • Dead set_scale() calls removed (we compute kg from raw directly)
 *   • Ultrasonic constant folded: 0.0343/2 → 0.01715
 *   • Serial.print + println used instead of "[HTTP] ..." + json concatenation
 *
 * Added features:
 *   • Periodic LCD refresh every 5 s in solar mode (live weight between 60 s sends)
 *   • INA219 init check — logs a warning if sensor missing, keeps device safe in idle mode
 *   • Task watchdog (30 s) — auto-resets ESP32 if loop hangs on a sensor/WiFi/HTTP call
 *   • HTTP retry — one retry after a 2 s backoff if POST fails
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_INA219.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include "HX711.h"

/* ================= WIFI / BACKEND ================= */
const char* ssid      = "TABAO_FAM";
const char* password  = "JOAN062199";
const char* serverURL = "https://sincere-creativity-production.up.railway.app/api/bin-data";
const char* DEVICE_ID = "ESP32_001";

/* ================= PINS ================= */
#define LED_POWER   26   // Green  — solid ON in solar mode, OFF in idle mode
#define LED_WIFI     4   // Yellow — blink while connecting, solid when connected (solar mode)
#define LED_BLUE     2   // Blue   — solid in solar mode, goes LOW during HTTP POST
#define SDA_PIN     13
#define SCL_PIN     14
#define TRIG1        5
#define ECHO1       18
#define TRIG2       17
#define ECHO2       16
#define HX1_DT      19
#define HX1_SCK     23
#define HX2_DT      21
#define HX2_SCK     22
#define MQ1_AO      34
#define MQ2_AO      32
#define BATTERY_PIN 33

/* ================= CALIBRATION ================= */
// Bin 1: empty=58cm, full=14.8cm  (50% at 34cm)
// Bin 2: empty=48cm, full=13.8cm  (50% at 29cm)
#define BIN1_EMPTY         58.0f
#define BIN2_EMPTY         48.0f
#define BIN1_FULL          14.8f
#define BIN2_FULL          13.8f

// HX711 scale factors — for local display/debug only; raw ADC is sent to backend
#define SCALE1              21564.0f   // raw counts per kg — Bin 1 (calibrated)
#define SCALE2              21201.0f   // raw counts per kg — Bin 2 (calibrated)
// Suppress HX711 noise below this weight (raw drift after tare ≈ ±5000 counts ≈ 42g)
#define WEIGHT_DEADBAND_KG 0.05f

#define GAS_THRESHOLD      500

/* ================= TIMING ================= */
#define SENSOR_WAKE_STABILIZE_MS  3000UL
#define GAS_CONFIRM_STABILIZE_MS  5000UL
#define SOLAR_SEND_INTERVAL_MS   60000UL    // 1 min
#define IDLE_SEND_INTERVAL_MS   600000UL    // 10 min
#define LCD_REFRESH_MS            5000UL    // solar-mode periodic LCD refresh
#define WDT_TIMEOUT_S                30     // reset ESP32 if loop stalls this long
#define HTTP_RETRY_DELAY_MS       2000UL

/* ================= OBJECTS ================= */
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_INA219    ina219;
HX711 scale1;
HX711 scale2;

/* ================= STATE ================= */
unsigned long solarTimer     = 0;
unsigned long idleTimer      = 0;
unsigned long lastLcdRefresh = 0;
bool bootSendPending = true;
bool sensorsAwake    = false;
bool lastSolarMode   = true;
bool ina219OK        = false;

/* ================= SENSOR VALUES ================= */
// Raw — sent to backend
float d1_cm, d2_cm;
long  hx1Raw, hx2Raw;
int   gas1, gas2;

// Derived — local display/debug only
float bin1Level, bin2Level;
float bin1Weight, bin2Weight;

// Battery / solar
float battery_voltage;
float charge_current;
float charge_power;

/* ================= HELPERS ================= */
float readUltrasonicFiltered(int trig, int echo) {
  float total = 0.0f;
  int   count = 0;

  for (int i = 0; i < 5; i++) {
    digitalWrite(trig, LOW);  delayMicroseconds(2);
    digitalWrite(trig, HIGH); delayMicroseconds(10);
    digitalWrite(trig, LOW);
    long duration = pulseIn(echo, HIGH, 30000);
    if (duration > 0) { total += duration * 0.01715f; count++; }
    delay(20);
  }

  return count ? total / count : -1.0f;
}

float getPercent(float dist, float emptyDist, float fullDist) {
  float level = (emptyDist - dist) / (emptyDist - fullDist) * 100.0f;
  return constrain(level, 0.0f, 100.0f);
}

const char* resetReasonToString(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:   return "Unknown";
    case ESP_RST_POWERON:   return "Power-on";
    case ESP_RST_EXT:       return "External pin";
    case ESP_RST_SW:        return "Software reset";
    case ESP_RST_PANIC:     return "Exception/panic";
    case ESP_RST_INT_WDT:   return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "Task watchdog";
    case ESP_RST_WDT:       return "Other watchdog";
    case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
    case ESP_RST_BROWNOUT:  return "Brownout";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "Unmapped";
  }
}

/* ================= WIFI ================= */
void sleepWiFi() {
  if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool maintainWiFi(unsigned long timeoutMs = 15000) {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println(F("[WiFi] Reconnecting..."));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid, password);

  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_WIFI, HIGH); delay(200);
    digitalWrite(LED_WIFI, LOW);  delay(200);
    esp_task_wdt_reset();
    if (millis() - started >= timeoutMs) {
      Serial.println(F("[WiFi] Timeout"));
      return false;
    }
  }

  Serial.print(F("[WiFi] IP: "));
  Serial.println(WiFi.localIP());
  return true;
}

/* ================= POWER / WAKE ================= */
void wakeSensors(const char* reason, unsigned long stabilizeMs = SENSOR_WAKE_STABILIZE_MS) {
  if (!sensorsAwake) {
    Serial.printf("[Wake] Sensors ON for %s\n", reason);
    scale1.power_up();
    scale2.power_up();
    sensorsAwake = true;
  }

  if (stabilizeMs > 0) {
    delay(stabilizeMs);
    // Discard a few noisy post-wake samples
    scale1.get_value(3);
    scale2.get_value(3);
  }
}

void sleepSensors() {
  if (!sensorsAwake) return;
  Serial.println(F("[Sleep] Sensors idle"));
  scale1.power_down();
  scale2.power_down();
  sensorsAwake = false;
}

bool quickGasDetected() {
  gas1 = analogRead(MQ1_AO);
  gas2 = analogRead(MQ2_AO);
  return gas1 >= GAS_THRESHOLD || gas2 >= GAS_THRESHOLD;
}

/* ================= READ ALL SENSORS ================= */
void readAllSensors() {
  d1_cm = readUltrasonicFiltered(TRIG1, ECHO1);
  d2_cm = readUltrasonicFiltered(TRIG2, ECHO2);
  bin1Level = (d1_cm >= 0) ? getPercent(d1_cm, BIN1_EMPTY, BIN1_FULL) : 0.0f;
  bin2Level = (d2_cm >= 0) ? getPercent(d2_cm, BIN2_EMPTY, BIN2_FULL) : 0.0f;

  hx1Raw = scale1.get_value(10);
  hx2Raw = scale2.get_value(10);
  if (hx1Raw < 0) hx1Raw = -hx1Raw;    // load cell wired inverted — flip sign
  if (hx2Raw < 0) hx2Raw = -hx2Raw;
  bin1Weight = (float)hx1Raw / SCALE1;
  bin2Weight = (float)hx2Raw / SCALE2;
  if (bin1Weight < WEIGHT_DEADBAND_KG) bin1Weight = 0.0f;
  if (bin2Weight < WEIGHT_DEADBAND_KG) bin2Weight = 0.0f;

  gas1 = analogRead(MQ1_AO);
  gas2 = analogRead(MQ2_AO);
  battery_voltage = (analogRead(BATTERY_PIN) / 4095.0f) * 3.3f * 4.0f;

  if (ina219OK) {
    charge_current = ina219.getCurrent_mA() / 1000.0f;
    charge_power   = ina219.getPower_mW()   / 1000.0f;
  } else {
    charge_current = 0.0f;
    charge_power   = 0.0f;
  }

  // Format weight as grams (<1 kg) or kg — mirrors a weighing-scale display
  char w1[8], w2[8];
  if (bin1Weight < 1.0f) snprintf(w1, sizeof(w1), "%dg",   (int)(bin1Weight * 1000));
  else                   snprintf(w1, sizeof(w1), "%.1fkg", bin1Weight);
  if (bin2Weight < 1.0f) snprintf(w2, sizeof(w2), "%dg",   (int)(bin2Weight * 1000));
  else                   snprintf(w2, sizeof(w2), "%.1fkg", bin2Weight);

  Serial.printf("[S] d1=%.1fcm(%.0f%%) d2=%.1fcm(%.0f%%) hx1=%ld(%s) hx2=%ld(%s) g1=%d g2=%d batt=%.2fV I=%.3fA P=%.2fW\n",
    d1_cm, bin1Level, d2_cm, bin2Level, hx1Raw, w1, hx2Raw, w2, gas1, gas2, battery_voltage, charge_current, charge_power);
}

bool confirmGasState(const char* reason) {
  wakeSensors(reason, GAS_CONFIRM_STABILIZE_MS);
  readAllSensors();
  return gas1 >= GAS_THRESHOLD || gas2 >= GAS_THRESHOLD;
}

/* ================= LCD ================= */
void printWeightField(float kg) {
  if      (kg == 0.0f) lcd.print(F("----"));
  else if (kg < 1.0f)  { lcd.print((int)(kg * 1000)); lcd.print(F("g")); }
  else                  { lcd.print(kg, 1);             lcd.print(F("kg")); }
}

void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("B1: "));
  printWeightField(bin1Weight);

  lcd.setCursor(0, 1);
  lcd.print(F("B2: "));
  printWeightField(bin2Weight);
}

/* ================= HTTP ================= */
bool postJSON(const String& json) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, serverURL);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(json);
  bool ok  = (code == 200 || code == 201);

  if (ok)            Serial.printf("[HTTP] OK %d\n", code);
  else if (code > 0) Serial.printf("[HTTP] FAIL %d\n", code);
  else               Serial.printf("[HTTP] ERR %d: %s\n", code, http.errorToString(code).c_str());

  http.end();
  return ok;
}

void sendData(const char* eventTag = nullptr) {
  if (!maintainWiFi()) {
    Serial.println(F("[HTTP] No WiFi, skip"));
    return;
  }

  digitalWrite(LED_BLUE, LOW);

  // Build JSON in a single String with pre-allocated buffer — no heap thrash
  String json;
  json.reserve(220);
  json  = F("{\"device_id\":\""); json += DEVICE_ID; json += '"';
  if (eventTag) { json += F(",\"event\":\""); json += eventTag; json += '"'; }
  json += F(",\"battery_voltage\":"); json += String(battery_voltage, 2);
  json += F(",\"bin_1\":{\"distance_cm\":"); json += String(d1_cm, 1);
  json += F(",\"hx711_raw\":"); json += hx1Raw;
  json += F(",\"mq_raw\":");    json += gas1; json += '}';
  json += F(",\"bin_2\":{\"distance_cm\":"); json += String(d2_cm, 1);
  json += F(",\"hx711_raw\":"); json += hx2Raw;
  json += F(",\"mq_raw\":");    json += gas2; json += F("}}");

  Serial.print(F("[HTTP] Sending: "));
  Serial.println(json);

  // One retry on failure (covers transient TLS / DNS / server hiccups)
  if (!postJSON(json)) {
    Serial.println(F("[HTTP] Retrying..."));
    delay(HTTP_RETRY_DELAY_MS);
    esp_task_wdt_reset();
    if (maintainWiFi(10000)) postJSON(json);
  }

  digitalWrite(LED_BLUE, HIGH);
  updateLCD();
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  delay(200);

  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("[Boot] Reset reason: %d (%s)\n", (int)reason, resetReasonToString(reason));

  // Task watchdog — auto-reset if loop stalls > WDT_TIMEOUT_S
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);

  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT);
  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_BLUE,  OUTPUT);
  pinMode(LED_WIFI,  OUTPUT);
  digitalWrite(LED_POWER, LOW);
  digitalWrite(LED_BLUE,  LOW);
  digitalWrite(LED_WIFI,  LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.print(F("Booting..."));

  // INA219 init check — without this sensor, solar/idle mode detection is impossible
  ina219OK = ina219.begin();
  if (!ina219OK) {
    Serial.println(F("[WARN] INA219 not found — solar detection disabled, staying in idle mode"));
    lcd.setCursor(0, 1);
    lcd.print(F("INA219 missing!"));
    delay(1500);
  }

  scale1.begin(HX1_DT, HX1_SCK);
  scale2.begin(HX2_DT, HX2_SCK);
  scale1.tare();
  scale2.tare();
  sensorsAwake = true;

  Serial.print(F("[Setup] Connecting to WiFi: "));
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  maintainWiFi();

  lcd.clear();
  lcd.print(WiFi.status() == WL_CONNECTED ? F("WiFi OK") : F("WiFi Retry"));
  delay(1000);

  // Read sensors at boot; defer the first POST until loop() to avoid heavy
  // network/TLS work during startup.
  readAllSensors();
}

/* ================= LOOP ================= */
void loop() {
  esp_task_wdt_reset();

  float current     = ina219OK ? (ina219.getCurrent_mA() / 1000.0f) : 0.0f;
  bool  solarMode   = ina219OK && (current > 0.1f);
  bool  gasDetected = quickGasDetected();

  if (bootSendPending && millis() >= 5000UL) {
    sendData("boot");
    bootSendPending = false;
    solarTimer = idleTimer = lastLcdRefresh = millis();
  }

  if (solarMode != lastSolarMode) {
    if (solarMode) wakeSensors("solar mode", SENSOR_WAKE_STABILIZE_MS);
    else           { sleepSensors(); sleepWiFi(); }
    lastSolarMode = solarMode;
  }

  /* -------- SOLAR MODE -------- */
  if (solarMode) {
    wakeSensors("solar mode", 0);
    maintainWiFi();

    digitalWrite(LED_POWER, HIGH);
    digitalWrite(LED_WIFI,  HIGH);
    digitalWrite(LED_BLUE,  HIGH);
    lcd.backlight();

    if (gasDetected) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("!! GAS ALERT !!"));

      if (confirmGasState("gas_detected")) {
        sendData("gas_detected");
        while (quickGasDetected()) {
          delay(5000);
          esp_task_wdt_reset();
        }
        if (!confirmGasState("gas_normal")) sendData("gas_normal");
      } else {
        Serial.println(F("[Gas] Cleared before confirmed send"));
      }
    }

    // Live LCD refresh between 60s sends — updates weight every 5s
    if (millis() - lastLcdRefresh >= LCD_REFRESH_MS) {
      readAllSensors();
      updateLCD();
      lastLcdRefresh = millis();
    }

    if (millis() - solarTimer >= SOLAR_SEND_INTERVAL_MS) {
      wakeSensors("solar sample", SENSOR_WAKE_STABILIZE_MS);
      readAllSensors();
      sendData();
      solarTimer = millis();
    }
  }

  /* -------- IDLE MODE -------- */
  else {
    digitalWrite(LED_POWER, LOW);
    digitalWrite(LED_WIFI,  LOW);
    digitalWrite(LED_BLUE,  LOW);
    lcd.noBacklight();

    if (gasDetected) {
      if (confirmGasState("gas_detected")) {
        sendData("gas_detected");
        while (quickGasDetected()) {
          delay(5000);
          esp_task_wdt_reset();
        }
        if (!confirmGasState("gas_normal")) sendData("gas_normal");
      } else {
        Serial.println(F("[Gas] Cleared before confirmed send"));
      }
      sleepSensors();
      sleepWiFi();
    }

    if (millis() - idleTimer >= IDLE_SEND_INTERVAL_MS) {
      wakeSensors("idle sample", SENSOR_WAKE_STABILIZE_MS);
      readAllSensors();
      sendData();
      idleTimer = millis();
      sleepSensors();
      sleepWiFi();
    }
  }
}
