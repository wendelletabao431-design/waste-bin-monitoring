#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_INA219.h>
#include "HX711.h"

/* ================= WIFI / BACKEND ================= */
const char* ssid      = "TABAO_FAM";
const char* password  = "JOAN062199";
const char* serverURL = "https://sincere-creativity-production.up.railway.app/api/bin-data";
const char* DEVICE_ID = "ESP32_001";

/* ================= LED ================= */
#define LED_POWER 27
#define LED_WIFI  4

/* ================= I2C ================= */
#define SDA_PIN 13
#define SCL_PIN 14

/* ================= PINS ================= */
#define TRIG1 5
#define ECHO1 18
#define TRIG2 17
#define ECHO2 16

#define HX1_DT  19
#define HX1_SCK 23
#define HX2_DT  21
#define HX2_SCK 22

#define MQ1_AO 34
#define MQ2_AO 32
#define BATTERY_PIN 33

#define SCALE1 87.1f
#define SCALE2 87.1f
#define GAS_THRESHOLD 500

LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_INA219 ina219;
HX711 scale1;
HX711 scale2;

/* ================= TIMERS ================= */
unsigned long solarTimer       = 0;
unsigned long batteryTimer     = 0;
unsigned long batterySendTimer = 0;

/* ================= VARIABLES ================= */
float d1_cm, d2_cm;
long hx1Raw, hx2Raw;
int gas1, gas2;

float battery_voltage;
float charge_current;
float charge_power;

bool lastMode = false;
bool isSending = false;

/* ================= WIFI CONTROL ================= */
void connectWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_WIFI, HIGH); delay(200);
    digitalWrite(LED_WIFI, LOW);  delay(200);
  }
  digitalWrite(LED_WIFI, HIGH);
  Serial.println(" CONNECTED");
}

void disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  digitalWrite(LED_WIFI, LOW);
  Serial.println("WiFi OFF");
}

/* ================= SENSOR ================= */
float readUltrasonicFiltered(int trig, int echo) {
  float total = 0; int count = 0;
  for (int i = 0; i < 5; i++) {
    digitalWrite(trig, LOW); delayMicroseconds(2);
    digitalWrite(trig, HIGH); delayMicroseconds(10);
    digitalWrite(trig, LOW);
    long duration = pulseIn(echo, HIGH, 30000);
    if (duration > 0) {
      total += duration * 0.0343f / 2.0f;
      count++;
    }
    delay(20);
  }
  return (count == 0) ? -1.0f : total / count;
}

void readAllSensors() {
  d1_cm = readUltrasonicFiltered(TRIG1, ECHO1);
  d2_cm = readUltrasonicFiltered(TRIG2, ECHO2);

  hx1Raw = scale1.get_value(10);
  hx2Raw = scale2.get_value(10);

  gas1 = analogRead(MQ1_AO);
  gas2 = analogRead(MQ2_AO);

  int raw = analogRead(BATTERY_PIN);
  float adc = (raw / 4095.0f) * 3.3f;
  battery_voltage = adc * 4.0f;

  charge_current = ina219.getCurrent_mA() / 1000.0f;
  charge_power   = ina219.getPower_mW() / 1000.0f;
}

/* ================= SEND DATA ================= */
void sendData(String tag="", String src="") {

  isSending = true;
  digitalWrite(LED_POWER, HIGH);

  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, serverURL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  if (tag!="") json += "\"event\":\"" + tag + "\",";
  json += "\"battery_voltage\":" + String(battery_voltage,2) + ",";

  json += "\"bin_1\":{\"distance_cm\":" + String(d1_cm,1) +
          ",\"hx711_raw\":" + String(hx1Raw) +
          ",\"mq_raw\":" + String(gas1) + "},";

  json += "\"bin_2\":{\"distance_cm\":" + String(d2_cm,1) +
          ",\"hx711_raw\":" + String(hx2Raw) +
          ",\"mq_raw\":" + String(gas2) + "}";

  json += "}";

  Serial.println("\n[DATA SEND]");
  Serial.println(json);

  int code = http.POST(json);
  Serial.print("[HTTP] "); Serial.println(code);

  http.end();

  isSending = false;

  if (charge_power <= 0.3f) {
    digitalWrite(LED_POWER, LOW);
    disconnectWiFi();
  }
}

/* ================= SEND BATTERY ONLY ================= */
void sendBatteryOnly() {

  isSending = true;
  digitalWrite(LED_POWER, HIGH);

  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, serverURL);
  http.addHeader("Content-Type", "application/json");

  String json = "{\"device_id\":\"" + String(DEVICE_ID) +
                "\",\"battery_voltage\":" + String(battery_voltage,2) + "}";

  Serial.println("\n[BATTERY SEND]");
  Serial.println(json);

  int code = http.POST(json);
  Serial.print("[HTTP] "); Serial.println(code);

  http.end();

  isSending = false;
  digitalWrite(LED_POWER, LOW);

  disconnectWiFi();
}

/* ================= SETUP ================= */
void setup() {

  Serial.begin(115200);

  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);

  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);

  digitalWrite(LED_POWER, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.noBacklight();

  ina219.begin();

  scale1.begin(HX1_DT, HX1_SCK);
  scale2.begin(HX2_DT, HX2_SCK);

  scale1.set_scale(SCALE1);
  scale2.set_scale(SCALE2);

  scale1.tare();
  scale2.tare();
}

/* ================= LOOP ================= */
void loop() {

  charge_power = ina219.getPower_mW() / 1000.0f;
  charge_current = ina219.getCurrent_mA() / 1000.0f;

  bool solarMode = (charge_power > 0.3f && charge_current > 0.05f);

  /* ===== LED CONTROL ===== */
  if (solarMode) {
    digitalWrite(LED_POWER, HIGH);
  } else {
    if (!isSending) digitalWrite(LED_POWER, LOW);
  }

  /* ===== LCD MODE ===== */
  if (solarMode != lastMode) {
    lcd.clear(); lcd.backlight();
    lcd.print(solarMode ? "SOLAR MODE" : "BATTERY MODE");
    delay(2000);
    if (!solarMode) lcd.noBacklight();
    lastMode = solarMode;
  }

  /* ===== SOLAR DISPLAY ===== */
  if (solarMode) {
    readAllSensors();

    lcd.setCursor(0,0);
    lcd.print("V:"); lcd.print(battery_voltage,1);

    lcd.setCursor(8,0);
    lcd.print("I:"); lcd.print(charge_current,1);

    lcd.setCursor(0,1);
    lcd.print("P:"); lcd.print(charge_power,1); lcd.print("W ");
  }

  /* ===== GAS EVENT ===== */
  gas1 = analogRead(MQ1_AO);
  gas2 = analogRead(MQ2_AO);

  if (gas1 >= GAS_THRESHOLD || gas2 >= GAS_THRESHOLD) {
    readAllSensors();
    sendData("gas_detected","GAS");
  }

  /* ===== BATTERY FAST SEND ===== */
  if (millis() - batterySendTimer >= 60000) {
    int raw = analogRead(BATTERY_PIN);
    battery_voltage = (raw / 4095.0f) * 3.3f * 4.0f;

    sendBatteryOnly();
    batterySendTimer = millis();
  }

  /* ===== NORMAL SEND ===== */
  if (solarMode) {
    if (millis() - solarTimer >= 60000) {
      readAllSensors();
      sendData("","SOLAR");
      solarTimer = millis();
    }
  } else {
    if (millis() - batteryTimer >= 1200000) {
      readAllSensors();
      sendData("","BATTERY");
      batteryTimer = millis();
    }
  }
} 
