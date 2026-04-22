#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_INA219.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include "HX711.h"

/* ================= WIFI ================= */
const char* ssid      = "TABAO1997";
const char* password  = "JuliaTango1999";
const char* serverURL = "https://sincere-creativity-production.up.railway.app/api/bin-data";
const char* DEVICE_ID = "ESP32_001";

/* ================= PINS ================= */
#define LED_POWER   26
#define LED_WIFI     4
#define LED_BLUE     2
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
// Bin 1 — kept exactly from esp32_firmware_test_backend
#define EMPTY_OFFSET_BIN1  313000.0f
#define BIN1_RAW_PER_KG     22600.0f

// Bin 2 — same approach, calibrate separately when hardware is ready
#define EMPTY_OFFSET_BIN2  313000.0f
#define BIN2_RAW_PER_KG     22600.0f

#define WEIGHT_DEADBAND_KG   0.05f
#define GAS_THRESHOLD         500

/* ================= TIMING ================= */
// Kept exactly from esp32_firmware_test_backend
#define HX_INTERVAL          200UL
#define OTHER_INTERVAL       300UL
#define LCD_REFRESH_MS       500UL
#define SEND_INTERVAL_MS   30000UL    // solar mode — 30 s

#define IDLE_SEND_INTERVAL_MS  600000UL   // idle mode — 10 min
#define GAS_CONFIRM_MS           5000UL
#define HTTP_RETRY_DELAY_MS      2000UL
#define WDT_TIMEOUT_S              30

/* ================= OBJECTS ================= */
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_INA219   ina219;
HX711 scale1;
HX711 scale2;

/* ================= STATE ================= */
bool ina219OK     = false;
bool lastSolarMode = true;
bool bootSendPending = true;

unsigned long lastHX      = 0;
unsigned long lastOther   = 0;
unsigned long lastSend    = 0;
unsigned long lastLcd     = 0;
unsigned long solarTimer  = 0;
unsigned long idleTimer   = 0;

/* ================= SENSOR VALUES ================= */
long  hx1Raw = 0;
long  hx2Raw = 0;
float w1 = 0, lastW1 = 0;
float w2 = 0, lastW2 = 0;
float d1 = -1, d2 = -1;
int   g1 = 0, g2 = 0;
float batt = 0;
float charge_current = 0;

/* ================= RESET REASON ================= */
const char* resetReasonToString(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "Power-on";
    case ESP_RST_SW:       return "Software reset";
    case ESP_RST_PANIC:    return "Exception/panic";
    case ESP_RST_INT_WDT:  return "Interrupt watchdog";
    case ESP_RST_TASK_WDT: return "Task watchdog";
    case ESP_RST_BROWNOUT: return "Brownout";
    default:               return "Other";
  }
}

/* ================= WIFI ================= */
bool maintainWiFi(unsigned long timeoutMs = 15000) {
  if (WiFi.status() == WL_CONNECTED) return true;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  unsigned long t = millis();

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_WIFI, !digitalRead(LED_WIFI));
    delay(300);
    esp_task_wdt_reset();
    if (millis() - t > timeoutMs) return false;
  }

  digitalWrite(LED_WIFI, HIGH);
  return true;
}

void sleepWiFi() {
  if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

/* ================= ULTRASONIC ================= */
float readUltrasonic(int trig, int echo) {
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long duration = pulseIn(echo, HIGH, 30000);
  if (duration == 0) return -1;

  return duration * 0.01715f;
}

/* ================= WEIGHT ================= */
// Kept exactly from esp32_firmware_test_backend — fixed offset, no tare
float getWeight1() {
  hx1Raw = scale1.read_average(15);
  float kg = (EMPTY_OFFSET_BIN1 - hx1Raw) / BIN1_RAW_PER_KG;

  if (kg < 0) kg = 0;
  if (kg < WEIGHT_DEADBAND_KG) kg = 0;
  if (abs(kg - lastW1) > 5.0) kg = lastW1;

  lastW1 = kg;
  return kg;
}

float getWeight2() {
  hx2Raw = scale2.read_average(15);
  float kg = (EMPTY_OFFSET_BIN2 - hx2Raw) / BIN2_RAW_PER_KG;

  if (kg < 0) kg = 0;
  if (kg < WEIGHT_DEADBAND_KG) kg = 0;
  if (abs(kg - lastW2) > 5.0) kg = lastW2;

  lastW2 = kg;
  return kg;
}

/* ================= GAS ================= */
bool quickGasDetected() {
  return g1 >= GAS_THRESHOLD || g2 >= GAS_THRESHOLD;
}

/* ================= LCD ================= */
void updateLCD(bool solar) {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(solar ? F("S W1:") : F("I W1:"));
  if (w1 == 0) lcd.print(F("----"));
  else { lcd.print(w1, 1); lcd.print(F("kg")); }

  lcd.setCursor(0, 1);
  lcd.print(F("W2:"));
  if (w2 == 0) lcd.print(F("----"));
  else { lcd.print(w2, 1); lcd.print(F("kg")); }
}

/* ================= HTTP ================= */
bool postJSON(const String& json) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, serverURL);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  http.addHeader(F("Content-Type"), F("application/json"));

  int code = http.POST(json);
  bool ok  = (code == 200 || code == 201);

  if (ok)            Serial.printf("[HTTP] OK %d\n", code);
  else if (code > 0) Serial.printf("[HTTP] FAIL %d\n", code);
  else               Serial.printf("[HTTP] ERR %d\n", code);

  http.end();
  return ok;
}

void sendData(const char* eventTag = nullptr) {
  if (!maintainWiFi()) return;

  digitalWrite(LED_BLUE, LOW);

  String json;
  json.reserve(220);
  json  = F("{\"device_id\":\""); json += DEVICE_ID; json += '"';
  if (eventTag) { json += F(",\"event\":\""); json += eventTag; json += '"'; }
  json += F(",\"battery_voltage\":"); json += String(batt, 2);
  json += F(",\"bin_1\":{\"distance_cm\":"); json += String(d1, 1);
  json += F(",\"hx711_raw\":"); json += hx1Raw;
  json += F(",\"mq_raw\":");    json += g1; json += F("}");
  json += F(",\"bin_2\":{\"distance_cm\":"); json += String(d2, 1);
  json += F(",\"hx711_raw\":"); json += hx2Raw;
  json += F(",\"mq_raw\":");    json += g2; json += F("}}");

  Serial.println(json);

  if (!postJSON(json)) {
    Serial.println(F("[HTTP] Retrying..."));
    delay(HTTP_RETRY_DELAY_MS);
    esp_task_wdt_reset();
    if (maintainWiFi(10000)) postJSON(json);
  }

  digitalWrite(LED_BLUE, HIGH);
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  delay(200);

  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("[Boot] Reset: %s\n", resetReasonToString(reason));

  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);

  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT);
  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_WIFI,  OUTPUT);
  pinMode(LED_BLUE,  OUTPUT);
  digitalWrite(LED_POWER, HIGH);
  digitalWrite(LED_BLUE,  LOW);
  digitalWrite(LED_WIFI,  LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.print(F("Booting..."));

  ina219OK = ina219.begin();
  if (!ina219OK) {
    Serial.println(F("[INA219] NOT FOUND — defaulting to solar mode"));
    lcd.setCursor(0, 1);
    lcd.print(F("INA219 missing!"));
    delay(1500);
  }

  // No tare — fixed offset calibration (kept from esp32_firmware_test_backend)
  scale1.begin(HX1_DT, HX1_SCK);
  scale2.begin(HX2_DT, HX2_SCK);
  delay(2000);

  WiFi.begin(ssid, password);
  maintainWiFi();

  lcd.clear();
  lcd.print(WiFi.status() == WL_CONNECTED ? F("WiFi OK") : F("WiFi Retry"));
  delay(1000);
}

/* ================= LOOP ================= */
void loop() {
  esp_task_wdt_reset();

  float current  = ina219OK ? ina219.getCurrent_mA() / 1000.0f : 1.0f;
  bool solarMode = (current >= 0.1f);

  /* ===== MODE SWITCH ===== */
  if (solarMode != lastSolarMode) {
    if (!solarMode) sleepWiFi();
    lastSolarMode = solarMode;
  }

  /* ===== BOOT SEND ===== */
  if (bootSendPending && millis() >= 5000UL) {
    sendData("boot");
    bootSendPending = false;
    solarTimer = idleTimer = millis();
  }

  /* ===== HX711 ISOLATED (kept from esp32_firmware_test_backend) ===== */
  if (millis() - lastHX >= HX_INTERVAL) {
    w1 = getWeight1();
    w2 = getWeight2();
    Serial.printf("[HX] RAW1=%ld W1=%.2fkg | RAW2=%ld W2=%.2fkg\n", hx1Raw, w1, hx2Raw, w2);
    lastHX = millis();
    return;
  }

  /* ===== OTHER SENSORS ===== */
  if (millis() - lastOther >= OTHER_INTERVAL) {
    d1   = readUltrasonic(TRIG1, ECHO1);
    d2   = readUltrasonic(TRIG2, ECHO2);
    g1   = analogRead(MQ1_AO);
    g2   = analogRead(MQ2_AO);
    batt = (analogRead(BATTERY_PIN) / 4095.0f) * 3.3f * 4.0f;
    charge_current = current;
    lastOther = millis();
  }

  /* ===== GAS ALERT ===== */
  if (quickGasDetected()) {
    Serial.println(F("[GAS] Alert detected!"));
    delay(GAS_CONFIRM_MS);
    esp_task_wdt_reset();
    if (g1 >= GAS_THRESHOLD || g2 >= GAS_THRESHOLD) {
      sendData("gas_detected");
      while (g1 >= GAS_THRESHOLD || g2 >= GAS_THRESHOLD) {
        g1 = analogRead(MQ1_AO);
        g2 = analogRead(MQ2_AO);
        delay(5000);
        esp_task_wdt_reset();
      }
      sendData("gas_normal");
    }
  }

  /* ===== SOLAR MODE ===== */
  if (solarMode) {
    digitalWrite(LED_POWER, HIGH);
    digitalWrite(LED_WIFI,  HIGH);
    digitalWrite(LED_BLUE,  HIGH);
    lcd.backlight();
    maintainWiFi();

    if (millis() - lastLcd >= LCD_REFRESH_MS) {
      updateLCD(true);
      lastLcd = millis();
    }

    if (millis() - solarTimer >= SEND_INTERVAL_MS) {
      sendData();
      solarTimer = millis();
    }
  }

  /* ===== IDLE MODE ===== */
  else {
    digitalWrite(LED_POWER, LOW);
    digitalWrite(LED_WIFI,  LOW);
    digitalWrite(LED_BLUE,  LOW);
    lcd.noBacklight();

    if (millis() - idleTimer >= IDLE_SEND_INTERVAL_MS) {
      maintainWiFi();
      sendData();
      idleTimer = millis();
      sleepWiFi();
    }
  }
}
