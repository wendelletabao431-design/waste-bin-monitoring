#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_INA219.h>
#include <esp_system.h>
#include "HX711.h"

/* ================= WIFI / BACKEND ================= */
const char* ssid      = "TABAO_FAM";
const char* password  = "JOAN062199";
const char* serverURL = "https://sincere-creativity-production.up.railway.app/api/bin-data";

// Use a different device id from production firmware so test data is easy to spot on the dashboard.
const char* DEVICE_ID = "ESP32_TEST_001";

/* ================= LED ================= */
#define LED_POWER 26
#define LED_WIFI  4
#define LED_BLUE  2

/* ================= I2C ================= */
#define SDA_PIN 13
#define SCL_PIN 14

/* ================= ULTRASONIC ================= */
#define TRIG1 5
#define ECHO1 18
#define TRIG2 17
#define ECHO2 16

/* ================= LOAD CELLS ================= */
#define HX1_DT  19
#define HX1_SCK 23
#define HX2_DT  21
#define HX2_SCK 22

/* ================= GAS ================= */
#define MQ1_AO 34
#define MQ2_AO 32
#define GAS_THRESHOLD 500

/* ================= BATTERY ================= */
#define BATTERY_PIN 33

/* ================= LOCAL TEST CALIBRATION ================= */
// Local weight display only. Backend still uses raw HX711 values as the source of truth.
float calibrationFactor1 = -20.8f;
float calibrationFactor2 = -20.8f;
float stableWeight1 = 0.0f;
float stableWeight2 = 0.0f;

/* ================= DISPLAY / TIMING ================= */
#define TEST_SEND_INTERVAL_MS 30000UL
#define GAS_RECHECK_MS 5000UL
#define LCD_REFRESH_MS 500UL

LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_INA219 ina219;
HX711 scale1;
HX711 scale2;

unsigned long lastSampleSend = 0;
unsigned long lastLcdRefresh = 0;
bool gasAlertActive = false;

// Raw values sent to backend
float d1_cm = -1.0f;
float d2_cm = -1.0f;
long hx1Raw = 0;
long hx2Raw = 0;
int gas1 = 0;
int gas2 = 0;
float battery_voltage = 0.0f;
float charge_current = 0.0f;
float charge_power = 0.0f;

// Local display/debug only
float weight1_grams = 0.0f;
float weight2_grams = 0.0f;

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

bool maintainWiFi(unsigned long timeoutMs = 15000) {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.println("[WiFi] Reconnecting...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid, password);

  unsigned long startedAt = millis();

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_WIFI, HIGH);
    delay(200);
    digitalWrite(LED_WIFI, LOW);
    delay(200);

    if (millis() - startedAt >= timeoutMs) {
      Serial.println("[WiFi] Connect timeout");
      return false;
    }
  }

  digitalWrite(LED_WIFI, HIGH);
  Serial.println("[WiFi] Connected: " + WiFi.localIP().toString());
  return true;
}

float readUltrasonicFiltered(int trig, int echo) {
  float total = 0.0f;
  int count = 0;

  for (int i = 0; i < 5; i++) {
    digitalWrite(trig, LOW);
    delayMicroseconds(2);
    digitalWrite(trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(trig, LOW);

    long duration = pulseIn(echo, HIGH, 30000);
    if (duration > 0) {
      total += duration * 0.0343f / 2.0f;
      count++;
    }

    delay(20);
  }

  return (count == 0) ? -1.0f : (total / count);
}

float computeDisplayWeightGrams(HX711& scale, float& stableWeight) {
  float rawWeight = scale.get_units(1);
  stableWeight = (stableWeight * 0.8f) + (rawWeight * 0.2f);

  float corrected = (0.9f * stableWeight) - 40.0f;
  if (corrected < 0.0f) {
    corrected = 0.0f;
  }

  return corrected;
}

void readAllSensors() {
  d1_cm = readUltrasonicFiltered(TRIG1, ECHO1);
  d2_cm = readUltrasonicFiltered(TRIG2, ECHO2);

  hx1Raw = scale1.get_value(10);
  hx2Raw = scale2.get_value(10);

  weight1_grams = computeDisplayWeightGrams(scale1, stableWeight1);
  weight2_grams = computeDisplayWeightGrams(scale2, stableWeight2);

  gas1 = analogRead(MQ1_AO);
  gas2 = analogRead(MQ2_AO);

  int raw = analogRead(BATTERY_PIN);
  float adc = (raw / 4095.0f) * 3.3f;
  battery_voltage = adc * 4.0f;

  charge_current = ina219.getCurrent_mA() / 1000.0f;
  charge_power   = ina219.getPower_mW() / 1000.0f;

  Serial.printf(
    "[Sensors] d1=%.1fcm d2=%.1fcm hx1=%ld hx2=%ld gas1=%d gas2=%d batt=%.2fV I=%.3fA P=%.2fW w1=%.0fg w2=%.0fg\n",
    d1_cm, d2_cm, hx1Raw, hx2Raw, gas1, gas2, battery_voltage, charge_current, charge_power, weight1_grams, weight2_grams
  );
}

void updateLCD() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("B1:");
  lcd.print(weight1_grams / 1000.0f, 1);
  lcd.print(" D:");
  if (d1_cm >= 0.0f) {
    lcd.print(d1_cm, 0);
  } else {
    lcd.print("ER");
  }

  lcd.setCursor(0, 1);
  lcd.print("B2:");
  lcd.print(weight2_grams / 1000.0f, 1);
  lcd.print(" D:");
  if (d2_cm >= 0.0f) {
    lcd.print(d2_cm, 0);
  } else {
    lcd.print("ER");
  }
}

void printWeightSummary() {
  Serial.println("========== TEST WEIGHT ==========");

  Serial.print("Bin1: ");
  if (weight1_grams < 1000.0f) {
    Serial.print(weight1_grams, 0);
    Serial.println(" g");
  } else {
    Serial.print(weight1_grams / 1000.0f, 1);
    Serial.println(" kg");
  }

  Serial.print("Bin2: ");
  if (weight2_grams < 1000.0f) {
    Serial.print(weight2_grams, 0);
    Serial.println(" g");
  } else {
    Serial.print(weight2_grams / 1000.0f, 1);
    Serial.println(" kg");
  }

  Serial.println("===============================\n");
}

void sendData(const char* eventTag = nullptr) {
  if (!maintainWiFi()) {
    Serial.println("[HTTP] Skipping send because WiFi is unavailable");
    return;
  }

  digitalWrite(LED_BLUE, LOW);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, serverURL);
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"device_id\":\"" + String(DEVICE_ID) + "\",";

  if (eventTag != nullptr && strlen(eventTag) > 0) {
    json += "\"event\":\"" + String(eventTag) + "\",";
  }

  json += "\"battery_voltage\":" + String(battery_voltage, 2) + ",";
  json += "\"bin_1\":{";
  json += "\"distance_cm\":" + String(d1_cm, 1) + ",";
  json += "\"hx711_raw\":" + String(hx1Raw) + ",";
  json += "\"mq_raw\":" + String(gas1);
  json += "},";
  json += "\"bin_2\":{";
  json += "\"distance_cm\":" + String(d2_cm, 1) + ",";
  json += "\"hx711_raw\":" + String(hx2Raw) + ",";
  json += "\"mq_raw\":" + String(gas2);
  json += "}}";

  Serial.println("[HTTP] Sending: " + json);

  int httpCode = http.POST(json);
  if (httpCode == 200 || httpCode == 201) {
    Serial.printf("[HTTP] POST OK (%d)\n", httpCode);
  } else if (httpCode > 0) {
    Serial.printf("[HTTP] POST FAILED (%d)\n", httpCode);
  } else {
    Serial.printf("[HTTP] POST ERROR (%d): %s\n", httpCode, http.errorToString(httpCode).c_str());
  }

  http.end();
  digitalWrite(LED_BLUE, HIGH);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.printf("[Boot] Reset reason: %d (%s)\n", resetReason, resetReasonToString(resetReason));

  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);

  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  digitalWrite(LED_POWER, HIGH);
  digitalWrite(LED_WIFI, LOW);
  digitalWrite(LED_BLUE, HIGH);

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.print("Test Boot...");

  ina219.begin();

  scale1.begin(HX1_DT, HX1_SCK);
  scale2.begin(HX2_DT, HX2_SCK);

  scale1.set_scale(calibrationFactor1);
  scale2.set_scale(calibrationFactor2);

  delay(3000);
  scale1.tare();
  scale2.tare();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  maintainWiFi();

  readAllSensors();
  updateLCD();
  printWeightSummary();
  sendData("test_boot");

  lastSampleSend = millis();
  lastLcdRefresh = millis();
}

void loop() {
  bool gasDetectedNow = (analogRead(MQ1_AO) >= GAS_THRESHOLD || analogRead(MQ2_AO) >= GAS_THRESHOLD);

  if (millis() - lastLcdRefresh >= LCD_REFRESH_MS) {
    readAllSensors();
    updateLCD();
    printWeightSummary();
    lastLcdRefresh = millis();
  }

  if (!gasAlertActive && gasDetectedNow) {
    Serial.println("[Gas] Gas detected, confirming...");
    delay(GAS_RECHECK_MS);
    readAllSensors();

    if (gas1 >= GAS_THRESHOLD || gas2 >= GAS_THRESHOLD) {
      gasAlertActive = true;
      sendData("test_gas_detected");
    }
  }

  if (gasAlertActive && !gasDetectedNow) {
    Serial.println("[Gas] Gas back to normal, confirming...");
    delay(GAS_RECHECK_MS);
    readAllSensors();

    if (gas1 < GAS_THRESHOLD && gas2 < GAS_THRESHOLD) {
      gasAlertActive = false;
      sendData("test_gas_normal");
    }
  }

  if (millis() - lastSampleSend >= TEST_SEND_INTERVAL_MS) {
    readAllSensors();
    sendData("test_sample");
    lastSampleSend = millis();
  }
}
