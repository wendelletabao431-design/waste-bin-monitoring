#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "HX711.h"

/* ================= WIFI ================= */
const char* ssid      = "TABAO1997";
const char* password  = "JuliaTango1999";
const char* serverURL = "https://sincere-creativity-production.up.railway.app/api/bin-data";
const char* DEVICE_ID = "ESP32_001";

/* ================= PINS ================= */
#define LED_POWER   26
#define LED_WIFI     4
#define SDA_PIN     13
#define SCL_PIN     14

#define TRIG1        5
#define ECHO1       18

#define HX1_DT      19
#define HX1_SCK     23

#define MQ1_AO      34
#define BATTERY_PIN 33

/* ================= CALIBRATION ================= */
#define EMPTY_OFFSET_BIN1 313000.0f
#define BIN1_RAW_PER_KG   22600.0f
#define WEIGHT_DEADBAND_KG 0.05f

/* ================= TIMING ================= */
#define HX_INTERVAL       200UL
#define OTHER_INTERVAL    300UL
#define LCD_REFRESH_MS    500UL
#define SEND_INTERVAL_MS 30000UL

LiquidCrystal_I2C lcd(0x27, 16, 2);
HX711 scale1;

/* ================= VARIABLES ================= */
unsigned long lastHX = 0;
unsigned long lastOther = 0;
unsigned long lastSend = 0;
unsigned long lastLcd = 0;

long hx1Raw = 0;
float w1 = 0;
float lastW1 = 0;

float d1 = 0;
int g1 = 0;
float batt = 0;

/* ================= WIFI ================= */
bool maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  WiFi.begin(ssid, password);
  unsigned long t = millis();

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_WIFI, !digitalRead(LED_WIFI));
    delay(300);
    if (millis() - t > 15000) return false;
  }

  digitalWrite(LED_WIFI, HIGH);
  return true;
}

/* ================= ULTRASONIC ================= */
float readUltrasonic() {
  digitalWrite(TRIG1, LOW); delayMicroseconds(2);
  digitalWrite(TRIG1, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG1, LOW);

  long duration = pulseIn(ECHO1, HIGH, 30000);
  if (duration == 0) return -1;

  return duration * 0.01715f;
}

/* ================= WEIGHT ================= */
float getWeight() {

  // 🔥 TRUE RAW (FIXED)
  hx1Raw = scale1.read_average(15);

  float kg = (EMPTY_OFFSET_BIN1 - hx1Raw) / BIN1_RAW_PER_KG;

  if (kg < 0) kg = 0;
  if (kg < WEIGHT_DEADBAND_KG) kg = 0;

  // simple stability filter
  if (abs(kg - lastW1) > 5.0) {
    kg = lastW1;
  }

  lastW1 = kg;
  return kg;
}

/* ================= LCD ================= */
void updateLCD() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("W:");
  if (w1 == 0) lcd.print("----");
  else lcd.print(w1, 1);
  lcd.print("kg");

  lcd.setCursor(0, 1);
  lcd.print("D:");
  if (d1 >= 0) lcd.print(d1, 1);
  else lcd.print("ERR");
}

/* ================= HTTP ================= */
void sendData() {
  if (!maintainWiFi()) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, serverURL);
  http.addHeader("Content-Type", "application/json");

  String json;
  json  = "{\"device_id\":\""; json += DEVICE_ID; json += "\"";
  json += ",\"battery_voltage\":"; json += String(batt, 2);
  json += ",\"bin_1\":{\"distance_cm\":"; json += String(d1,1);
  json += ",\"hx711_raw\":"; json += hx1Raw;
  json += ",\"mq_raw\":"; json += g1; json += "}}";

  Serial.println(json);

  int code = http.POST(json);
  Serial.printf("[HTTP] %d\n", code);

  http.end();
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);

  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);

  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);

  digitalWrite(LED_POWER, HIGH);

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();

  scale1.begin(HX1_DT, HX1_SCK);
  delay(2000);

  // ⚠️ NO tare needed for raw-based calibration

  WiFi.begin(ssid, password);
  maintainWiFi();
}

/* ================= LOOP ================= */
void loop() {

  /* ===== HX711 (ISOLATED) ===== */
  if (millis() - lastHX >= HX_INTERVAL) {

    w1 = getWeight();

    Serial.printf("[HX] RAW=%ld  W=%.2fkg\n", hx1Raw, w1);

    lastHX = millis();
    return;   // 🔥 keeps HX stable
  }

  /* ===== OTHER SENSORS ===== */
  if (millis() - lastOther >= OTHER_INTERVAL) {

    d1 = readUltrasonic();
    g1 = analogRead(MQ1_AO);
    batt = (analogRead(BATTERY_PIN) / 4095.0f) * 3.3f * 4.0f;

    lastOther = millis();
  }

  /* ===== LCD ===== */
  if (millis() - lastLcd >= LCD_REFRESH_MS) {
    updateLCD();
    lastLcd = millis();
  }

  /* ===== SEND ===== */
  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    sendData();
    lastSend = millis();
  }
}