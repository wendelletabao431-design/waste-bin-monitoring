#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "HX711.h"
#include "esp_sleep.h"

/* ================= WIFI ================= */
const char* ssid = "TABAO_FAM";
const char* password = "JOAN062199";
const char* serverURL = "https://smart-trash-bin-production.up.railway.app/api/bin-data";

// IMPORTANT: Give your device a unique identifier
const char* deviceID = "ESP32_STATION_01";

/* ================= CALIBRATION ================= */
// CALIBRATION SOURCE: Raw Values = Calibration.docx
#define EMPTY_DISTANCE_CM 59.0   // Ultrasonic: reading when bin is empty
#define FULL_DISTANCE_CM  4.0    // Ultrasonic: reading when bin is full
#define OFFSET_CM         3.5    // Ultrasonic: correction offset

// HX711 calibration (per bin)
#define SCALE_FACTOR_BIN1 119800.0  // Bin 1: raw units per kg
#define SCALE_FACTOR_BIN2 117786.0  // Bin 2: raw units per kg
#define RAW_EMPTY_BIN1    451977    // Bin 1: raw reading when empty
#define RAW_EMPTY_BIN2    -491000   // Bin 2: raw reading when empty
#define MAX_WEIGHT_KG     20.0

#define SLEEP_TIME_SEC 3600        // 1 hour sleep

/* ================= LED PINS ================= */
#define LED_POWER   2
#define LED_WIFI    4
#define LED_SOURCE  26
#define POWER_SRC_PIN 27   // HIGH = Battery, LOW = Solar

/* ================= BIN 1 ================= */
#define TRIG1 5
#define ECHO1 18
#define HX1_DT 19
#define HX1_SCK 23
#define MQ1_AO 34
#define MQ1_DO 25     // ACTIVE LOW (LM393)

/* ================= BIN 2 ================= */
#define TRIG2 17
#define ECHO2 16
#define HX2_DT 21
#define HX2_SCK 22
#define MQ2_AO 32

/* ================= BATTERY ================= */
#define BATTERY_PIN 33

HX711 scale1;
HX711 scale2;

/* ================= FUNCTIONS ================= */

float readUltrasonicCM(uint8_t trig, uint8_t echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long duration = pulseIn(echo, HIGH, 30000);
  if (duration == 0) return -1;

  return duration * 0.0343 / 2.0;
}

float levelPercent(float d) {
  float correctedDistance = d + OFFSET_CM;
  float pct = (EMPTY_DISTANCE_CM - correctedDistance) /
              (EMPTY_DISTANCE_CM - FULL_DISTANCE_CM) * 100.0;
  return constrain(pct, 0.0, 100.0);
}

void connectWiFi() {
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    digitalWrite(LED_WIFI, HIGH);
    delay(300);
    digitalWrite(LED_WIFI, LOW);
    delay(300);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_WIFI, HIGH);
    Serial.println("✓ WiFi Connected");
  } else {
    Serial.println("✗ WiFi Connection Failed");
  }
}

void sendData(String payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("✗ WiFi not connected, skipping data send");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, serverURL);
  
  // CRITICAL: Tell Laravel we want JSON responses, not HTML redirects
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  Serial.println("\n===== SENDING DATA TO SERVER =====");
  Serial.println("Payload:");
  Serial.println(payload);
  
  int httpResponseCode = http.POST(payload);
  
  if (httpResponseCode > 0) {
    Serial.printf("✓ Server Response Code: %d\n", httpResponseCode);
    String response = http.getString();
    Serial.println("Server Response:");
    Serial.println(response);
    
    if (httpResponseCode == 200) {
      Serial.println("✓ Data successfully sent!");
    } else if (httpResponseCode == 302) {
      Serial.println("⚠ Server responded with redirect (302) - Check device_id and payload format");
    } else if (httpResponseCode == 422) {
      Serial.println("⚠ Validation error - Check JSON structure");
    }
  } else {
    Serial.printf("✗ HTTP Error code: %d\n", httpResponseCode);
  }
  
  Serial.println("==================================\n");
  
  http.end();
}

/* ================= SETUP ================= */

void setup() {

  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n===== SMART BIN STARTING =====");
  Serial.print("Device ID: ");
  Serial.println(deviceID);

  /* ===== LED Setup ===== */
  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);
  pinMode(LED_SOURCE, OUTPUT);
  pinMode(POWER_SRC_PIN, INPUT);
  digitalWrite(LED_POWER, HIGH);

  if (digitalRead(POWER_SRC_PIN))
    digitalWrite(LED_SOURCE, HIGH);
  else {
    for (int i = 0; i < 4; i++) {
      digitalWrite(LED_SOURCE, HIGH);
      delay(200);
      digitalWrite(LED_SOURCE, LOW);
      delay(200);
    }
  }

  /* ===== Sensor Setup ===== */
  pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT);
  pinMode(MQ1_DO, INPUT_PULLUP);

  scale1.begin(HX1_DT, HX1_SCK);
  scale2.begin(HX2_DT, HX2_SCK);

  scale1.set_scale(SCALE_FACTOR_BIN1);
  scale2.set_scale(SCALE_FACTOR_BIN2);

  scale1.tare();
  scale2.tare();

  delay(2000); // MQ warm-up

  /* ===== READ SENSORS ===== */

  float d1 = readUltrasonicCM(TRIG1, ECHO1);
  float d2 = readUltrasonicCM(TRIG2, ECHO2);

  long w1_raw = scale1.read_average(10);
  long w2_raw = scale2.read_average(10);

  // Calculated weight using calibration formula (for serial debug)
  // Formula: (raw - empty_raw) / scale
  float w1kg = constrain((w1_raw - RAW_EMPTY_BIN1) / SCALE_FACTOR_BIN1, 0.0f, MAX_WEIGHT_KG);
  float w2kg = constrain((w2_raw - RAW_EMPTY_BIN2) / SCALE_FACTOR_BIN2, 0.0f, MAX_WEIGHT_KG);

  int gas1_raw = analogRead(MQ1_AO);
  int gas2_raw = analogRead(MQ2_AO);

  bool gasDetected = (digitalRead(MQ1_DO) == LOW);  // ACTIVE LOW

  int battery_adc = analogRead(BATTERY_PIN);

  /* ===== SERIAL (INTERPRETED) ===== */

  Serial.println("\n===== SMART BIN STATUS =====");

  Serial.print("BIN1 Level (%): ");
  Serial.println(levelPercent(d1));

  Serial.print("BIN1 Weight (kg): ");
  Serial.println(w1kg, 2);

  Serial.print("BIN1 Gas: ");
  Serial.println(gasDetected ? "FLAMMABLE DETECTED" : "NORMAL");

  Serial.print("BIN2 Level (%): ");
  Serial.println(levelPercent(d2));

  Serial.print("BIN2 Weight (kg): ");
  Serial.println(w2kg, 2);

  Serial.println("============================\n");

  /* ===== SEND RAW DATA ===== */

  connectWiFi();

  String json = "{";
  json += "\"device_id\":\"" + String(deviceID) + "\",";  // REQUIRED FIELD
  json += "\"battery_adc\":" + String(battery_adc) + ",";
  json += "\"bin_1\":{";
  json += "\"distance_cm\":" + String(d1,1) + ",";
  json += "\"hx711_raw\":" + String(w1_raw) + ",";
  json += "\"mq_raw\":" + String(gas1_raw);
  json += "},";
  json += "\"bin_2\":{";
  json += "\"distance_cm\":" + String(d2,1) + ",";
  json += "\"hx711_raw\":" + String(w2_raw) + ",";
  json += "\"mq_raw\":" + String(gas2_raw);
  json += "}";
  json += "}";

  sendData(json);

  /* ===== WAKE SOURCES ===== */

  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_TIME_SEC * 1000000ULL);

  // Wake when gas detected (LOW)
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_25, 0);

  Serial.println("Entering deep sleep for 1 hour...");
  delay(1000);
  esp_deep_sleep_start();
}

/* ================= LOOP ================= */
void loop() {}
