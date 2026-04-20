#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
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

// Bin 1
#define TRIG1        5
#define ECHO1       18
#define HX1_DT      19
#define HX1_SCK     23
#define MQ1_AO      34

// Bin 2
#define TRIG2       17
#define ECHO2       16
#define HX2_DT      21
#define HX2_SCK     22
#define MQ2_AO      32

#define BATTERY_PIN 33

/* ================= CALIBRATION ================= */
// Bin 1 — confirmed calibration
#define HX1_OFFSET  10360
#define HX1_SCALE   -24050.0f

// Bin 2 — confirmed calibration
#define HX2_OFFSET  10360
#define HX2_SCALE   -25160.0f

// Suppress noise below 50g after tare
#define WEIGHT_DEADBAND_KG 0.05f

#define SEND_INTERVAL_MS 30000UL
#define LCD_REFRESH_MS     500UL

LiquidCrystal_I2C lcd(0x27, 16, 2);
HX711 scale1;
HX711 scale2;

unsigned long lastSend = 0;
unsigned long lastLcd  = 0;

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

/* ================= STABLE WEIGHT ================= */
// Same function for both bins — pass scale + calibration values
float getStableWeight(HX711& scale, int offset, float scaleF) {
  float total = 0;
  int   valid = 0;

  for (int i = 0; i < 10; i++) {
    long  raw = scale.get_value();
    float kg  = (raw - offset) / scaleF;

    if (kg > -5.0f && kg < 50.0f) {
      total += kg;
      valid++;
    }
    delay(10);
  }

  if (valid == 0) return 0.0f;

  float weight = total / valid;
  if (weight < WEIGHT_DEADBAND_KG) weight = 0.0f;

  return weight;
}

/* ================= LCD ================= */
// Shows weight as grams below 1 kg, kg above — ---- when nothing on scale
void printWeight(float kg) {
  if      (kg == 0.0f) lcd.print("----");
  else if (kg < 1.0f)  { lcd.print((int)(kg * 1000)); lcd.print("g"); }
  else                  { lcd.print(kg, 1);             lcd.print("kg"); }
}

void updateLCD(float w1, float d1, float w2, float d2) {
  lcd.clear();

  // Line 0: Bin 1 — weight + distance
  lcd.setCursor(0, 0);
  lcd.print("B1:");
  printWeight(w1);
  lcd.print(" D:");
  if (d1 >= 0) lcd.print((int)d1); else lcd.print("ER");

  // Line 1: Bin 2 — weight + distance
  lcd.setCursor(0, 1);
  lcd.print("B2:");
  printWeight(w2);
  lcd.print(" D:");
  if (d2 >= 0) lcd.print((int)d2); else lcd.print("ER");
}

/* ================= HTTP ================= */
void sendData(float w1, float d1, int g1, float w2, float d2, int g2, float batt) {
  if (!maintainWiFi()) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, serverURL);
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/json");

  // Backend expects hx711_raw (raw ADC counts), not kg
  // Reverse: raw = (kg * scale) + offset
  long hx1Raw = (long)(w1 * HX1_SCALE) + HX1_OFFSET;
  long hx2Raw = (long)(w2 * HX2_SCALE) + HX2_OFFSET;

  String json;
  json.reserve(220);
  json  = "{\"device_id\":\"";   json += DEVICE_ID;          json += "\"";
  json += ",\"battery_voltage\":"; json += String(batt, 2);
  json += ",\"bin_1\":{\"distance_cm\":"; json += String(d1, 1);
  json += ",\"hx711_raw\":";  json += hx1Raw;
  json += ",\"mq_raw\":";     json += g1;   json += "}";
  json += ",\"bin_2\":{\"distance_cm\":"; json += String(d2, 1);
  json += ",\"hx711_raw\":";  json += hx2Raw;
  json += ",\"mq_raw\":";     json += g2;   json += "}";
  json += "}";

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

    float w1   = getStableWeight(scale1, HX1_OFFSET, HX1_SCALE);
    float w2   = getStableWeight(scale2, HX2_OFFSET, HX2_SCALE);
    float d1   = readUltrasonic(TRIG1, ECHO1);
    float d2   = readUltrasonic(TRIG2, ECHO2);
    int   g1   = analogRead(MQ1_AO);
    int   g2   = analogRead(MQ2_AO);
    float batt = (analogRead(BATTERY_PIN) / 4095.0f) * 3.3f * 3.857f;

    Serial.printf("[DATA] W1=%.2fkg D1=%.1f G1=%d | W2=%.2fkg D2=%.1f G2=%d | Batt=%.2f\n",
                  w1, d1, g1, w2, d2, g2, batt);

    updateLCD(w1, d1, w2, d2);

    if (millis() - lastSend >= SEND_INTERVAL_MS) {
      sendData(w1, d1, g1, w2, d2, g2, batt);
      lastSend = millis();
    }

    lastLcd = millis();
  }
}
