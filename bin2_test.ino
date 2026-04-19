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
#define TRIG2       17
#define ECHO2       16
#define HX2_DT      21
#define HX2_SCK     22
#define MQ2_AO      32
#define BATTERY_PIN 33

/* ================= CALIBRATION ================= */
// Backend derives weight as: kg = hx711_raw / BIN2_RAW_PER_KG
// TODO: recalibrate with a known weight — same process as Bin 1
#define BIN2_RAW_PER_KG   21201.0f
#define WEIGHT_DEADBAND_KG  0.05f

#define SEND_INTERVAL_MS  30000UL
#define LCD_REFRESH_MS      500UL

LiquidCrystal_I2C lcd(0x27, 16, 2);
HX711 scale2;

unsigned long lastSend = 0;
unsigned long lastLcd  = 0;
long  hx2Raw = 0;

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
  digitalWrite(TRIG2, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG2, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG2, LOW);

  long duration = pulseIn(ECHO2, HIGH, 30000);
  if (duration == 0) return -1;

  return duration * 0.01715f;
}

/* ================= WEIGHT ================= */
// Returns display weight in kg; also updates global hx2Raw for sending
float readWeight() {
  hx2Raw = scale2.get_value(10);          // 10-sample average, raw ADC counts
  if (hx2Raw < 0) hx2Raw = -hx2Raw;      // handle inverted load cell wiring
  float kg = (float)hx2Raw / BIN2_RAW_PER_KG;

  Serial.printf("[HX711] raw=%ld  kg=%.4f\n", hx2Raw, kg);
  if (kg > 50.0f)              return 0.0f;
  if (kg < WEIGHT_DEADBAND_KG) return 0.0f;

  return kg;
}

/* ================= LCD ================= */
void updateLCD(float weight, float distance) {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("B2:");
  if      (weight == 0.0f) lcd.print("----");
  else if (weight < 1.0f)  { lcd.print((int)(weight * 1000)); lcd.print("g"); }
  else                      { lcd.print(weight, 1);            lcd.print("kg"); }

  lcd.setCursor(0, 1);
  lcd.print("D:");
  if (distance >= 0) lcd.print(distance, 1);
  else               lcd.print("ERR");
}

/* ================= HTTP ================= */
void sendData(float distance, int gas, float batt) {
  if (!maintainWiFi()) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, serverURL);
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/json");

  // Send raw ADC value directly — backend divides by BIN2_RAW_PER_KG to get kg
  String json;
  json.reserve(180);
  json  = "{\"device_id\":\"";       json += DEVICE_ID;        json += "\"";
  json += ",\"battery_voltage\":";   json += String(batt, 2);
  json += ",\"bin_2\":{\"distance_cm\":"; json += String(distance, 1);
  json += ",\"hx711_raw\":";         json += hx2Raw;
  json += ",\"mq_raw\":";            json += gas;  json += "}}";

  Serial.printf("[JSON] %s\n", json.c_str());

  int code = http.POST(json);
  Serial.printf("[HTTP] %d\n", code);

  http.end();
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);

  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);
  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_WIFI,  OUTPUT);
  pinMode(LED_BLUE,  OUTPUT);

  digitalWrite(LED_POWER, HIGH);

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();

  scale2.begin(HX2_DT, HX2_SCK);
  delay(3000);
  scale2.tare();

  WiFi.begin(ssid, password);
  maintainWiFi();
}

/* ================= LOOP ================= */
void loop() {
  if (millis() - lastLcd >= LCD_REFRESH_MS) {

    float weight   = readWeight();
    float distance = readUltrasonic();
    int   gas      = analogRead(MQ2_AO);
    float batt     = (analogRead(BATTERY_PIN) / 4095.0f) * 3.3f * 4.0f;

    Serial.printf("[DATA] W=%.2fkg D=%.1f Gas=%d Batt=%.2f\n",
                  weight, distance, gas, batt);

    updateLCD(weight, distance);

    if (millis() - lastSend >= SEND_INTERVAL_MS) {
      sendData(distance, gas, batt);
      lastSend = millis();
    }

    lastLcd = millis();
  }
}
