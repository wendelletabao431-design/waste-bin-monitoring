#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "HX711.h"

/* ================= WIFI / BACKEND ================= */
const char* ssid      = "TABAO_FAM";
const char* password  = "JOAN062199";
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

/* ================= THRESHOLDS / TIMING ================= */
#define GAS_THRESHOLD    500
#define SEND_INTERVAL_MS 30000UL
#define GAS_RECHECK_MS    5000UL
#define LCD_REFRESH_MS     500UL

// Production calibration — LCD shows what the backend will compute
#define BIN1_RAW_PER_KG  119800.0f
#define BIN2_RAW_PER_KG  117786.0f
// Suppress HX711 noise below this weight (raw drift after tare ≈ ±5000 counts ≈ 42g)
#define WEIGHT_DEADBAND_KG 0.05f

LiquidCrystal_I2C lcd(0x27, 16, 2);
HX711 scale1, scale2;

unsigned long lastSend = 0;
unsigned long lastLcd  = 0;
bool gasAlertActive    = false;

float d1_cm = -1.0f, d2_cm = -1.0f;
long  hx1Raw = 0,    hx2Raw = 0;
int   gas1 = 0,      gas2 = 0;
float battery_voltage = 0.0f;

/* ================= WIFI ================= */
bool maintainWiFi(unsigned long timeoutMs = 15000) {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println(F("[WiFi] Reconnecting..."));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid, password);

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_WIFI, HIGH); delay(200);
    digitalWrite(LED_WIFI, LOW);  delay(200);
    if (millis() - t >= timeoutMs) {
      Serial.println(F("[WiFi] Timeout"));
      return false;
    }
  }

  digitalWrite(LED_WIFI, HIGH);
  Serial.print(F("[WiFi] IP: "));
  Serial.println(WiFi.localIP());
  return true;
}

/* ================= SENSORS ================= */
float readUltrasonic(int trig, int echo) {
  float total = 0.0f;
  int n = 0;

  for (int i = 0; i < 5; i++) {
    digitalWrite(trig, LOW);  delayMicroseconds(2);
    digitalWrite(trig, HIGH); delayMicroseconds(10);
    digitalWrite(trig, LOW);
    long d = pulseIn(echo, HIGH, 30000);
    if (d > 0) { total += d * 0.01715f; n++; }
    delay(20);
  }

  return n ? total / n : -1.0f;
}

void readAllSensors() {
  d1_cm = readUltrasonic(TRIG1, ECHO1);
  d2_cm = readUltrasonic(TRIG2, ECHO2);
  hx1Raw = scale1.get_value(10);
  hx2Raw = scale2.get_value(10);
  gas1 = analogRead(MQ1_AO);
  gas2 = analogRead(MQ2_AO);
  battery_voltage = (analogRead(BATTERY_PIN) / 4095.0f) * 3.3f * 3.857f;

  Serial.printf("[S] d1=%.1f d2=%.1f hx1=%ld hx2=%ld g1=%d g2=%d batt=%.2fV\n",
    d1_cm, d2_cm, hx1Raw, hx2Raw, gas1, gas2, battery_voltage);
}

/* ================= LCD ================= */
void updateLCD() {
  float kg1 = hx1Raw / BIN1_RAW_PER_KG;
  float kg2 = hx2Raw / BIN2_RAW_PER_KG;
  if (kg1 < WEIGHT_DEADBAND_KG) kg1 = 0.0f;
  if (kg2 < WEIGHT_DEADBAND_KG) kg2 = 0.0f;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("B1:"));
  if      (kg1 == 0.0f) lcd.print(F("----"));
  else if (kg1 < 1.0f)  { lcd.print((int)(kg1 * 1000)); lcd.print('g'); }
  else                   { lcd.print(kg1, 1);             lcd.print(F("kg")); }
  lcd.print(F(" D:"));
  if (d1_cm >= 0) lcd.print((int)d1_cm); else lcd.print(F("ER"));

  lcd.setCursor(0, 1);
  lcd.print(F("B2:"));
  if      (kg2 == 0.0f) lcd.print(F("----"));
  else if (kg2 < 1.0f)  { lcd.print((int)(kg2 * 1000)); lcd.print('g'); }
  else                   { lcd.print(kg2, 1);             lcd.print(F("kg")); }
  lcd.print(F(" D:"));
  if (d2_cm >= 0) lcd.print((int)d2_cm); else lcd.print(F("ER"));
}

/* ================= HTTP ================= */
void sendData(const char* event = nullptr) {
  if (!maintainWiFi()) {
    Serial.println(F("[HTTP] No WiFi, skip"));
    return;
  }

  digitalWrite(LED_BLUE, LOW);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, serverURL);
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/json");

  String json;
  json.reserve(200);
  json  = F("{\"device_id\":\""); json += DEVICE_ID; json += '"';
  if (event) { json += F(",\"event\":\""); json += event; json += '"'; }
  json += F(",\"battery_voltage\":"); json += String(battery_voltage, 2);
  json += F(",\"bin_1\":{\"distance_cm\":"); json += String(d1_cm, 1);
  json += F(",\"hx711_raw\":"); json += hx1Raw;
  json += F(",\"mq_raw\":"); json += gas1; json += '}';
  json += F(",\"bin_2\":{\"distance_cm\":"); json += String(d2_cm, 1);
  json += F(",\"hx711_raw\":"); json += hx2Raw;
  json += F(",\"mq_raw\":"); json += gas2; json += F("}}");

  int code = http.POST(json);
  if (code > 0) Serial.printf("[HTTP] %d\n", code);
  else Serial.printf("[HTTP] ERR %d: %s\n", code, http.errorToString(code).c_str());

  http.end();
  digitalWrite(LED_BLUE, HIGH);
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("[Boot] Test firmware v2"));

  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT);
  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_POWER, HIGH);
  digitalWrite(LED_WIFI, LOW);
  digitalWrite(LED_BLUE, HIGH);

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.print(F("Test Boot..."));

  scale1.begin(HX1_DT, HX1_SCK);
  scale2.begin(HX2_DT, HX2_SCK);
  delay(3000);
  scale1.tare();
  scale2.tare();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  maintainWiFi();

  readAllSensors();
  updateLCD();
  sendData("test_boot");
  lastSend = lastLcd = millis();
}

/* ================= LOOP ================= */
void loop() {
  bool gasNow = analogRead(MQ1_AO) >= GAS_THRESHOLD || analogRead(MQ2_AO) >= GAS_THRESHOLD;

  if (millis() - lastLcd >= LCD_REFRESH_MS) {
    readAllSensors();
    updateLCD();
    lastLcd = millis();
  }

  if (!gasAlertActive && gasNow) {
    Serial.println(F("[Gas] Detected, confirming..."));
    delay(GAS_RECHECK_MS);
    readAllSensors();
    if (gas1 >= GAS_THRESHOLD || gas2 >= GAS_THRESHOLD) {
      gasAlertActive = true;
      sendData("test_gas_detected");
    }
  }

  if (gasAlertActive && !gasNow) {
    Serial.println(F("[Gas] Cleared, confirming..."));
    delay(GAS_RECHECK_MS);
    readAllSensors();
    if (gas1 < GAS_THRESHOLD && gas2 < GAS_THRESHOLD) {
      gasAlertActive = false;
      sendData("test_gas_normal");
    }
  }

  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    readAllSensors();
    sendData("test_sample");
    lastSend = millis();
  }
}
