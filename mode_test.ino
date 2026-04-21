#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_INA219.h>
#include "HX711.h"

/* ================= WIFI ================= */
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

/* ================= CALIBRATION ================= */
#define BIN1_RAW_PER_KG   22235.0f
#define BIN2_RAW_PER_KG   22235.0f
#define WEIGHT_DEADBAND_KG  0.05f

#define SEND_INTERVAL_MS  30000UL
#define LCD_REFRESH_MS     1000UL

LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_INA219   ina219;
HX711 scale1;
HX711 scale2;

bool ina219OK = false;

unsigned long lastSend = 0;
unsigned long lastLcd  = 0;
long hx1Raw = 0;
long hx2Raw = 0;

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
float readUltrasonic(int trig, int echo) {
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long duration = pulseIn(echo, HIGH, 30000);
  if (duration == 0) return -1;

  return duration * 0.01715f;
}

/* ================= WEIGHT ================= */
// Takes 10 individual samples with gaps, filters out-of-range readings,
// and averages the valid ones — prevents spikes from corrupting the result.
float stableWeight(HX711& scale, long& rawOut, float rawPerKg) {
  float total = 0;
  int   valid = 0;

  for (int i = 0; i < 10; i++) {
    long  r  = scale.get_value();
    if (r < 0) r = -r;
    float kg = (float)r / rawPerKg;
    if (kg >= 0.0f && kg < 50.0f) {
      total += kg;
      rawOut = r;
      valid++;
    }
    delay(15);
  }

  if (valid == 0) return 0.0f;

  float avg = total / valid;
  if (avg < WEIGHT_DEADBAND_KG) return 0.0f;
  return avg;
}

float readWeight1() {
  float kg = stableWeight(scale1, hx1Raw, BIN1_RAW_PER_KG);
  Serial.printf("[HX1] raw=%ld  kg=%.4f\n", hx1Raw, kg);
  return kg;
}

float readWeight2() {
  float kg = stableWeight(scale2, hx2Raw, BIN2_RAW_PER_KG);
  Serial.printf("[HX2] raw=%ld  kg=%.4f\n", hx2Raw, kg);
  return kg;
}

/* ================= LCD ================= */
// Line 0: V: 12.7V  I: 2.34A
// Line 1: P: 29.3W
void updateLCD() {
  if (!ina219OK) return;

  float voltage = ina219.getBusVoltage_V();
  float current = ina219.getCurrent_mA() / 1000.0f;
  float power   = ina219.getPower_mW()   / 1000.0f;

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("V:");
  lcd.print(voltage, 1);
  lcd.print("V  I:");
  lcd.print(current, 2);
  lcd.print("A");

  lcd.setCursor(0, 1);
  lcd.print("P:");
  lcd.print(power, 1);
  lcd.print("W");
}

/* ================= HTTP ================= */
void sendData(float d1, int g1, float d2, int g2, float batt) {
  if (!maintainWiFi()) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, serverURL);
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/json");

  String json;
  json.reserve(220);
  json  = "{\"device_id\":\"";            json += DEVICE_ID;       json += "\"";
  json += ",\"battery_voltage\":";        json += String(batt, 2);
  json += ",\"bin_1\":{\"distance_cm\":"; json += String(d1, 1);
  json += ",\"hx711_raw\":";              json += hx1Raw;
  json += ",\"mq_raw\":";                 json += g1;  json += "}";
  json += ",\"bin_2\":{\"distance_cm\":"; json += String(d2, 1);
  json += ",\"hx711_raw\":";              json += hx2Raw;
  json += ",\"mq_raw\":";                 json += g2;  json += "}";
  json += "}";

  Serial.printf("[JSON] %s\n", json.c_str());

  int code = http.POST(json);
  Serial.printf("[HTTP] %d\n", code);

  http.end();
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);

  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT);
  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_WIFI,  OUTPUT);
  pinMode(LED_BLUE,  OUTPUT);

  digitalWrite(LED_POWER, HIGH);

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();

  ina219OK = ina219.begin();
  if (!ina219OK) Serial.println("[INA219] NOT FOUND");

  scale1.begin(HX1_DT, HX1_SCK);
  scale2.begin(HX2_DT, HX2_SCK);
  delay(3000);
  scale1.tare();
  scale2.tare();

  WiFi.begin(ssid, password);
  maintainWiFi();
}

/* ================= LOOP ================= */
void loop() {
  if (millis() - lastLcd >= LCD_REFRESH_MS) {

    float w1   = readWeight1();
    delay(100);
    float w2   = readWeight2();
    float d1   = readUltrasonic(TRIG1, ECHO1);
    float d2   = readUltrasonic(TRIG2, ECHO2);
    int   g1   = analogRead(MQ1_AO);
    int   g2   = analogRead(MQ2_AO);
    float batt = (analogRead(BATTERY_PIN) / 4095.0f) * 3.3f * 3.857f;

    Serial.printf("[DATA] W1=%.2fkg D1=%.1f G1=%d | W2=%.2fkg D2=%.1f G2=%d | Batt=%.2f\n",
                  w1, d1, g1, w2, d2, g2, batt);

    updateLCD();

    if (millis() - lastSend >= SEND_INTERVAL_MS) {
      sendData(d1, g1, d2, g2, batt);
      lastSend = millis();
    }

    lastLcd = millis();
  }
}
