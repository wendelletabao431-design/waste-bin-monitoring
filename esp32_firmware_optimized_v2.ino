#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include "HX711.h"

/* ================= WIFI / BACKEND ================= */
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
#define BIN1_EMPTY         58.0f
#define BIN2_EMPTY         48.0f
#define BIN1_FULL          14.8f
#define BIN2_FULL          13.8f
#define SCALE1             21564.0f
#define SCALE2             21201.0f
#define WEIGHT_DEADBAND_KG  0.05f
#define GAS_THRESHOLD        500

/* ================= TIMING ================= */
#define SEND_INTERVAL_MS   60000UL
#define LCD_REFRESH_MS      5000UL
#define WDT_TIMEOUT_S          30
#define HTTP_RETRY_DELAY_MS  2000UL
#define GAS_CONFIRM_MS       5000UL

/* ================= OBJECTS ================= */
LiquidCrystal_I2C lcd(0x27, 16, 2);
HX711 scale1;
HX711 scale2;

/* ================= STATE ================= */
unsigned long sendTimer      = 0;
unsigned long lastLcdRefresh = 0;
bool bootSendPending = true;

/* ================= SENSOR VALUES ================= */
float d1_cm, d2_cm;
long  hx1Raw, hx2Raw;
int   gas1, gas2;
float bin1Level, bin2Level;
float bin1Weight, bin2Weight;
float battery_voltage;

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
  return constrain((emptyDist - dist) / (emptyDist - fullDist) * 100.0f, 0.0f, 100.0f);
}

const char* resetReasonToString(esp_reset_reason_t reason) {
  switch (reason) {
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

/* ================= READ ALL SENSORS ================= */
void readAllSensors() {
  d1_cm = readUltrasonicFiltered(TRIG1, ECHO1);
  d2_cm = readUltrasonicFiltered(TRIG2, ECHO2);
  bin1Level = (d1_cm >= 0) ? getPercent(d1_cm, BIN1_EMPTY, BIN1_FULL) : 0.0f;
  bin2Level = (d2_cm >= 0) ? getPercent(d2_cm, BIN2_EMPTY, BIN2_FULL) : 0.0f;

  hx1Raw = scale1.get_value(10);
  hx2Raw = scale2.get_value(10);
  if (hx1Raw < 0) hx1Raw = -hx1Raw;
  if (hx2Raw < 0) hx2Raw = -hx2Raw;
  bin1Weight = (float)hx1Raw / SCALE1;
  bin2Weight = (float)hx2Raw / SCALE2;
  if (bin1Weight > 50.0f || bin1Weight < WEIGHT_DEADBAND_KG) bin1Weight = 0.0f;
  if (bin2Weight > 50.0f || bin2Weight < WEIGHT_DEADBAND_KG) bin2Weight = 0.0f;

  gas1 = analogRead(MQ1_AO);
  gas2 = analogRead(MQ2_AO);
  battery_voltage = (analogRead(BATTERY_PIN) / 4095.0f) * 3.3f * 4.0f;

  char w1[8], w2[8];
  if (bin1Weight < 1.0f) snprintf(w1, sizeof(w1), "%dg",   (int)(bin1Weight * 1000));
  else                   snprintf(w1, sizeof(w1), "%.1fkg", bin1Weight);
  if (bin2Weight < 1.0f) snprintf(w2, sizeof(w2), "%dg",   (int)(bin2Weight * 1000));
  else                   snprintf(w2, sizeof(w2), "%.1fkg", bin2Weight);

  Serial.printf("[S] d1=%.1fcm(%.0f%%) d2=%.1fcm(%.0f%%) hx1=%ld(%s) hx2=%ld(%s) g1=%d g2=%d batt=%.2fV\n",
    d1_cm, bin1Level, d2_cm, bin2Level, hx1Raw, w1, hx2Raw, w2, gas1, gas2, battery_voltage);
}

bool quickGasDetected() {
  gas1 = analogRead(MQ1_AO);
  gas2 = analogRead(MQ2_AO);
  return gas1 >= GAS_THRESHOLD || gas2 >= GAS_THRESHOLD;
}

/* ================= LCD ================= */
void printWeightField(float kg) {
  if      (kg == 0.0f) lcd.print(F("----"));
  else if (kg < 1.0f)  { lcd.print((int)(kg * 1000)); lcd.print(F("g")); }
  else                  { lcd.print(kg, 1); lcd.print(F("kg")); }
}

void updateLCD() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("B1:"));
  printWeightField(bin1Weight);
  lcd.print(F(" D:"));
  if (d1_cm >= 0) lcd.print((int)d1_cm); else lcd.print(F("ER"));

  lcd.setCursor(0, 1);
  lcd.print(F("B2:"));
  printWeightField(bin2Weight);
  lcd.print(F(" D:"));
  if (d2_cm >= 0) lcd.print((int)d2_cm); else lcd.print(F("ER"));
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
  pinMode(LED_BLUE,  OUTPUT);
  pinMode(LED_WIFI,  OUTPUT);
  digitalWrite(LED_POWER, HIGH);
  digitalWrite(LED_BLUE,  LOW);
  digitalWrite(LED_WIFI,  LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.print(F("Booting..."));

  scale1.begin(HX1_DT, HX1_SCK);
  scale2.begin(HX2_DT, HX2_SCK);
  delay(3000);
  scale1.tare();
  scale2.tare();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  maintainWiFi();

  lcd.clear();
  lcd.print(WiFi.status() == WL_CONNECTED ? F("WiFi OK") : F("WiFi Retry"));
  delay(1000);

  readAllSensors();
}

/* ================= LOOP ================= */
void loop() {
  esp_task_wdt_reset();

  if (bootSendPending && millis() >= 5000UL) {
    sendData("boot");
    bootSendPending = false;
    sendTimer = lastLcdRefresh = millis();
  }

  // Gas alert
  if (quickGasDetected()) {
    Serial.println(F("[Gas] Alert!"));
    delay(GAS_CONFIRM_MS);
    esp_task_wdt_reset();
    if (gas1 >= GAS_THRESHOLD || gas2 >= GAS_THRESHOLD) {
      readAllSensors();
      sendData("gas_detected");
      while (gas1 >= GAS_THRESHOLD || gas2 >= GAS_THRESHOLD) {
        gas1 = analogRead(MQ1_AO);
        gas2 = analogRead(MQ2_AO);
        delay(5000);
        esp_task_wdt_reset();
      }
      sendData("gas_normal");
    }
  }

  // LCD refresh
  if (millis() - lastLcdRefresh >= LCD_REFRESH_MS) {
    readAllSensors();
    updateLCD();
    lastLcdRefresh = millis();
  }

  // Periodic send
  if (millis() - sendTimer >= SEND_INTERVAL_MS) {
    readAllSensors();
    sendData();
    sendTimer = millis();
  }

  digitalWrite(LED_POWER, HIGH);
  digitalWrite(LED_WIFI,  WiFi.status() == WL_CONNECTED ? HIGH : LOW);
  digitalWrite(LED_BLUE,  HIGH);
  lcd.backlight();
}
