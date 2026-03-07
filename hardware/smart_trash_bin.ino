/**
 * Smart Trash Bin ESP32 Firmware
 * 
 * UPDATED VERSION - Compatible with Laravel Backend
 * 
 * Changes from original:
 * 1. Added device_id (MAC address) to payload
 * 2. Updated serverURL to match backend endpoint
 * 3. Added response handling for debugging
 * 
 * Hardware:
 * - ESP32 with WiFi
 * - 2x Ultrasonic sensors (HC-SR04)
 * - 2x HX711 load cells
 * - 2x MQ gas sensors
 * - Battery voltage monitoring
 * - Power source detection (Solar/Battery)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "HX711.h"

/* ================= WIFI CONFIGURATION ================= */
// TODO: Update these with your actual credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// IMPORTANT: Update this to your Laravel server IP/domain
// Format: http://YOUR_SERVER_IP/api/bin-data
const char* serverURL = "http://192.168.1.100/api/bin-data";

/* ================= CALIBRATION CONSTANTS ================= */
// These values are mirrored in Laravel config/sensors.php
// If you change them here, update the backend too!
// CALIBRATION SOURCE: Raw Values = Calibration.docx

#define EMPTY_DISTANCE_CM 59.0   // Ultrasonic: reading when bin is empty (cm)
#define FULL_DISTANCE_CM  4.0    // Ultrasonic: reading when bin is full (cm)
#define OFFSET_CM         3.5    // Ultrasonic: correction offset (cm)

#define SCALE_FACTOR_BIN1 119800.0  // HX711 Bin 1: raw units per kg
#define SCALE_FACTOR_BIN2 117786.0  // HX711 Bin 2: raw units per kg
#define RAW_EMPTY_BIN1    451977    // HX711 Bin 1: raw reading when empty
#define RAW_EMPTY_BIN2    -491000   // HX711 Bin 2: raw reading when empty
#define MAX_WEIGHT_KG     20.0      // Maximum weight capacity

#define MQ_NORMAL_MAX     300    // Gas: normal air max (100-300)
#define MQ_ELEVATED_MIN   300    // Gas: elevated threshold
#define MQ_DANGEROUS_MIN  600    // Gas: dangerous/flammable threshold (600-900+)

#define BATTERY_MAX_V 12.6       // Full battery voltage (3S Li-ion)
#define BATTERY_MIN_V 9.0        // Empty battery voltage (3S Li-ion)

/* ================= LED INDICATOR PINS ================= */
#define LED_POWER 2              // Power indicator (always on)
#define LED_WIFI  4              // WiFi status (blinks when connecting)
#define LED_SOURCE 26            // Power source indicator
#define POWER_SRC_PIN 27         // HIGH = Battery, LOW = Solar

/* ================= BIN 1 SENSOR PINS ================= */
#define TRIG1 5
#define ECHO1 18
#define HX1_DT 19
#define HX1_SCK 23
#define MQ1 34

/* ================= BIN 2 SENSOR PINS ================= */
#define TRIG2 17
#define ECHO2 16
#define HX2_DT 21
#define HX2_SCK 22
#define MQ2 32

/* ================= BATTERY MONITORING ================= */
#define BATTERY_PIN 35

/* ================= TIMING ================= */
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 10000; // Send data every 10 seconds

unsigned long lastBlink = 0;
bool blinkState = false;

/* ================= SENSOR OBJECTS ================= */
HX711 scale1;
HX711 scale2;

/* ================= HELPER FUNCTIONS ================= */

/**
 * Read distance from ultrasonic sensor
 * @return Distance in centimeters, or -1 if error
 */
float readUltrasonicCM(uint8_t trig, uint8_t echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long duration = pulseIn(echo, HIGH, 30000);
  if (duration == 0) return -1;
  return duration * 0.0343f / 2.0f;
}

/**
 * Calculate fill percentage from distance (for serial debug only)
 * Note: Backend does this calculation - this is just for local display
 * 
 * Formula: (empty - distance - offset) / (empty - full) * 100
 */
float levelPercent(float d) {
  float correctedDistance = d + OFFSET_CM;
  float level =
    (EMPTY_DISTANCE_CM - correctedDistance) /
    (EMPTY_DISTANCE_CM - FULL_DISTANCE_CM) * 100.0f;
  return constrain(level, 0.0f, 100.0f);
}

/**
 * Convert a raw ADC battery reading to battery voltage.
 */
float batteryVoltageFromRaw(int raw) {
  return (raw / 4095.0f) * 3.3f * 4.21f;
}

/**
 * Calculate battery percentage (for serial debug only)
 * Note: Backend does this calculation - this is just for local display
 */
float batteryPercent(float voltage) {
  float percent =
    (voltage - BATTERY_MIN_V) /
    (BATTERY_MAX_V - BATTERY_MIN_V) * 100.0f;
  return constrain(percent, 0.0f, 100.0f);
}

/**
 * Send JSON payload to Laravel backend
 * @param payload JSON string to send
 */
void sendData(const String& payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected - skipping send");
    return;
  }
  
  HTTPClient http;
  http.begin(serverURL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  
  Serial.println("Sending to: " + String(serverURL));
  Serial.println("Payload: " + payload);
  
  int httpCode = http.POST(payload);
  
  if (httpCode > 0) {
    Serial.printf("HTTP Response: %d\n", httpCode);
    if (httpCode == HTTP_CODE_OK) {
      String response = http.getString();
      Serial.println("Response: " + response);
    }
  } else {
    Serial.printf("HTTP Error: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Smart Trash Bin System ===");

  // Configure GPIO pins
  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);

  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);
  pinMode(LED_SOURCE, OUTPUT);
  pinMode(POWER_SRC_PIN, INPUT);

  // Power LED always on
  digitalWrite(LED_POWER, HIGH);

  // Initialize load cells
  Serial.println("Initializing HX711 sensors...");
  scale1.begin(HX1_DT, HX1_SCK);
  scale1.set_scale(SCALE_FACTOR_BIN1);
  scale1.tare();

  scale2.begin(HX2_DT, HX2_SCK);
  scale2.set_scale(SCALE_FACTOR_BIN2);
  scale2.tare();
  Serial.println("HX711 sensors ready");

  // Connect to WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    // Blink WiFi LED while connecting
    digitalWrite(LED_WIFI, millis() % 600 < 300);
    delay(100);
  }
  
  digitalWrite(LED_WIFI, HIGH);
  Serial.println("WiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
}

/* ================= MAIN LOOP ================= */
void loop() {
  // Update power source LED
  bool onBattery = digitalRead(POWER_SRC_PIN);
  if (!onBattery) {
    // Solar power - blink LED
    if (millis() - lastBlink >= 300) {
      blinkState = !blinkState;
      digitalWrite(LED_SOURCE, blinkState);
      lastBlink = millis();
    }
  } else {
    // Battery power - solid LED
    digitalWrite(LED_SOURCE, HIGH);
  }

  // Send data at regular intervals
  if (millis() - lastSendTime >= SEND_INTERVAL) {
    
    /* ===== READ ALL SENSORS (RAW VALUES) ===== */
    
    // Ultrasonic distance sensors
    float d1 = readUltrasonicCM(TRIG1, ECHO1);
    float d2 = readUltrasonicCM(TRIG2, ECHO2);

    // HX711 load cells (raw values for backend)
    long rawW1 = scale1.read_average(5);
    long rawW2 = scale2.read_average(5);

    // Calculated weight using calibration formula (for serial debug only)
    // Formula: (raw - empty_raw) / scale
    float w1kg = constrain((rawW1 - RAW_EMPTY_BIN1) / SCALE_FACTOR_BIN1, 0.0f, MAX_WEIGHT_KG);
    float w2kg = constrain((rawW2 - RAW_EMPTY_BIN2) / SCALE_FACTOR_BIN2, 0.0f, MAX_WEIGHT_KG);

    // MQ gas sensors (raw ADC values)
    int gas1 = analogRead(MQ1);
    int gas2 = analogRead(MQ2);

    // Battery voltage
    int battRaw = analogRead(BATTERY_PIN);
    float battVoltage = batteryVoltageFromRaw(battRaw);
    float battPct = batteryPercent(battVoltage);

    /* ===== SERIAL DEBUG OUTPUT (Interpreted Values) ===== */
    Serial.println("\n================================");
    Serial.println("Device ID: " + WiFi.macAddress());
    Serial.print("Battery (%): "); Serial.println(battPct);
    Serial.print("Battery (V): "); Serial.println(battVoltage, 2);
    Serial.print("Battery RAW: "); Serial.println(battRaw);
    Serial.print("Power Source: "); Serial.println(onBattery ? "BATTERY" : "SOLAR");

    Serial.println("\nBIN 1:");
    Serial.print("  Distance (cm): "); Serial.println(d1);
    Serial.print("  Level (%): "); Serial.println(d1 >= 0 ? levelPercent(d1) : 0);
    Serial.print("  Weight RAW: "); Serial.println(rawW1);
    Serial.print("  Weight (kg): "); Serial.println(w1kg, 2);
    Serial.print("  Gas RAW: "); Serial.println(gas1);
    Serial.print("  Gas Status: "); 
    if (gas1 >= MQ_DANGEROUS_MIN) {
      Serial.println("DANGEROUS/FLAMMABLE");
    } else if (gas1 >= MQ_ELEVATED_MIN) {
      Serial.println("ELEVATED");
    } else {
      Serial.println("NORMAL");
    }

    Serial.println("\nBIN 2:");
    Serial.print("  Distance (cm): "); Serial.println(d2);
    Serial.print("  Level (%): "); Serial.println(d2 >= 0 ? levelPercent(d2) : 0);
    Serial.print("  Weight RAW: "); Serial.println(rawW2);
    Serial.print("  Weight (kg): "); Serial.println(w2kg, 2);
    Serial.print("  Gas RAW: "); Serial.println(gas2);
    Serial.print("  Gas Status: "); 
    if (gas2 >= MQ_DANGEROUS_MIN) {
      Serial.println("DANGEROUS/FLAMMABLE");
    } else if (gas2 >= MQ_ELEVATED_MIN) {
      Serial.println("ELEVATED");
    } else {
      Serial.println("NORMAL");
    }

    /* ===== BUILD JSON PAYLOAD ===== */
    // CRITICAL: Include device_id for backend identification
    // Backend handles all derivation except the ADC-to-voltage conversion.
    
    String payload = "{";
    payload += "\"device_id\":\"" + WiFi.macAddress() + "\",";  // <-- REQUIRED!
    payload += "\"battery_voltage\":" + String(battVoltage, 3) + ",";
    payload += "\"bin_1\":{";
    payload += "\"distance_cm\":" + String(d1, 1) + ",";
    payload += "\"hx711_raw\":" + String(rawW1) + ",";
    payload += "\"mq_raw\":" + String(gas1);
    payload += "},";
    payload += "\"bin_2\":{";
    payload += "\"distance_cm\":" + String(d2, 1) + ",";
    payload += "\"hx711_raw\":" + String(rawW2) + ",";
    payload += "\"mq_raw\":" + String(gas2);
    payload += "}";
    payload += "}";

    /* ===== SEND TO BACKEND ===== */
    sendData(payload);
    
    lastSendTime = millis();
  }
}
