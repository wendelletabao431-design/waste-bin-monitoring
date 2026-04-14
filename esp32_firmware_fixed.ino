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

// Unique identifier for this ESP32 unit.
// Change this if you deploy more than one device.
const char* DEVICE_ID = "ESP32_001";

/* ================= LED ================= */
#define LED_POWER 26  // Green  — solid ON in solar mode, OFF in idle mode
#define LED_WIFI  4   // Yellow — blink while connecting, solid when connected (solar mode only)
#define LED_BLUE  2   // Blue   — solid when idle, blinks LOW during HTTP send (solar mode only)

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

/* ================= BATTERY ================= */
#define BATTERY_PIN 33

/* ================= CALIBRATION ================= */
// Local fill% display only — backend performs authoritative derivation
// Bin 1: empty=58cm, full=10cm  (50% at 34cm, 90% at 14.8cm)
// Bin 2: empty=48cm, full=10cm  (50% at 29cm, 90% at 13.8cm)
#define BIN1_EMPTY 58.0f
#define BIN2_EMPTY 48.0f
#define BIN_FULL   10.0f

// HX711 scale factors for local Serial debug display only; raw ADC is sent to backend
#define SCALE1 90.4f
#define SCALE2 92.6f

#define GAS_THRESHOLD 500

/* ================= OBJECTS ================= */
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_INA219    ina219;
HX711 scale1;
HX711 scale2;

/* ================= TIMERS ================= */
unsigned long solarTimer = 0;
unsigned long idleTimer  = 0;

/* ================= SENSOR VALUES ================= */
// Raw values — sent to the backend as-is
float d1_cm, d2_cm;       // Ultrasonic raw distances (cm)
long  hx1Raw, hx2Raw;     // HX711 raw ADC values
int   gas1, gas2;         // MQ sensor ADC values

// Derived values — Serial debug only, no longer displayed on LCD
float bin1Level, bin2Level;   // Fill % (derived from distance)
float bin1Weight, bin2Weight; // Weight in kg (derived from raw ADC)

// Battery / solar
float battery_voltage;
float charge_current;
float charge_power;

/* ================= ULTRASONIC FILTER ================= */
float readUltrasonicFiltered(int trig, int echo) {
  float total = 0;
  int   count = 0;

  for (int i = 0; i < 5; i++) {
    digitalWrite(trig, LOW);
    delayMicroseconds(2);
    digitalWrite(trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(trig, LOW);

    long duration = pulseIn(echo, HIGH, 30000);

    if (duration > 0) {
      float dist = duration * 0.0343f / 2.0f;
      total += dist;
      count++;
    }
    delay(20);
  }

  if (count == 0) return -1.0f; // Sensor error sentinel
  return total / count;
}

/* ================= BIN LEVEL (local debug) ================= */
float getPercent(float dist, float emptyDist) {
  float level = (emptyDist - dist) / (emptyDist - BIN_FULL) * 100.0f;
  return constrain(level, 0.0f, 100.0f);
}

/* ================= WIFI ================= */
void maintainWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    // Blink LED_WIFI while connecting; main loop sets solid/off per mode after this
    while (WiFi.status() != WL_CONNECTED) {
      digitalWrite(LED_WIFI, HIGH); delay(200);
      digitalWrite(LED_WIFI, LOW);  delay(200);
    }
    Serial.println("[WiFi] Connected: " + WiFi.localIP().toString());
  }
}

/* ================= READ ALL SENSORS ================= */
void readAllSensors() {
  // --- Ultrasonic (raw cm) ---
  d1_cm = readUltrasonicFiltered(TRIG1, ECHO1);
  d2_cm = readUltrasonicFiltered(TRIG2, ECHO2);

  // Derived fill% for Serial debug only
  bin1Level = (d1_cm >= 0) ? getPercent(d1_cm, BIN1_EMPTY) : 0.0f;
  bin2Level = (d2_cm >= 0) ? getPercent(d2_cm, BIN2_EMPTY) : 0.0f;

  // --- Load cells ---
  // get_value() returns the raw ADC sum — required by backend
  hx1Raw = scale1.get_value(10);
  hx2Raw = scale2.get_value(10);

  // Derived weight for Serial debug only
  bin1Weight = scale1.get_units(10);
  bin2Weight = scale2.get_units(10);

  // --- Gas sensors ---
  gas1 = analogRead(MQ1_AO);
  gas2 = analogRead(MQ2_AO);

  // --- Battery voltage via voltage divider (pin 33) ---
  int   raw = analogRead(BATTERY_PIN);
  float adc = (raw / 4095.0f) * 3.3f;
  battery_voltage = adc * 4.0f;

  // --- INA219 (solar charging) ---
  charge_current = ina219.getCurrent_mA() / 1000.0f;
  charge_power   = ina219.getPower_mW()   / 1000.0f;

  Serial.printf("[Sensors] d1=%.1fcm d2=%.1fcm hx1=%ld hx2=%ld gas1=%d gas2=%d batt=%.2fV I=%.3fA P=%.2fW\n",
    d1_cm, d2_cm, hx1Raw, hx2Raw, gas1, gas2, battery_voltage, charge_current, charge_power);
}

/* ================= SEND DATA ================= */
void sendData(String eventTag = "") {

  maintainWiFi();

  // Blink blue LED during HTTP send — turn LOW for duration of POST.
  // In solar mode this creates a visible blink; in idle mode it is already LOW.
  // Main loop restores correct state per mode on next iteration.
  digitalWrite(LED_BLUE, LOW);

  WiFiClientSecure client;
  client.setInsecure(); // Skip TLS cert verification for Railway

  HTTPClient http;
  http.begin(client, serverURL);
  http.addHeader("Content-Type", "application/json");

  // Build nested JSON matching backend schema:
  // {
  //   "device_id": "...",
  //   "battery_voltage": ...,
  //   "bin_1": { "distance_cm": ..., "hx711_raw": ..., "mq_raw": ... },
  //   "bin_2": { "distance_cm": ..., "hx711_raw": ..., "mq_raw": ... }
  // }
  String json = "{";

  json += "\"device_id\":\"" + String(DEVICE_ID) + "\",";

  if (eventTag != "") {
    json += "\"event\":\"" + eventTag + "\",";
  }

  json += "\"battery_voltage\":" + String(battery_voltage, 2) + ",";

  // Bin 1 — raw sensor values
  json += "\"bin_1\":{";
  json += "\"distance_cm\":" + String(d1_cm, 1) + ",";
  json += "\"hx711_raw\":"   + String(hx1Raw)   + ",";
  json += "\"mq_raw\":"      + String(gas1);
  json += "},";

  // Bin 2 — raw sensor values
  json += "\"bin_2\":{";
  json += "\"distance_cm\":" + String(d2_cm, 1) + ",";
  json += "\"hx711_raw\":"   + String(hx2Raw)   + ",";
  json += "\"mq_raw\":"      + String(gas2);
  json += "}";

  json += "}";

  Serial.println("[HTTP] Sending: " + json);

  int httpCode = http.POST(json);
  String response = http.getString();
  http.end();

  if (httpCode == 200 || httpCode == 201) {
    Serial.printf("[HTTP] POST OK (%d): %s\n", httpCode, response.c_str());
  } else {
    Serial.printf("[HTTP] POST FAILED (%d): %s\n", httpCode, response.c_str());
  }

  // Update LCD with power measurements only.
  // Backlight is controlled by the main loop per mode — no change here.
  // Line 1: "V:XX.XV I:X.XXA"  (15 chars max)
  // Line 2: "P:XX.XXW        "
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("V:");
  lcd.print(battery_voltage, 1);
  lcd.print("V I:");
  lcd.print(charge_current, 2);
  lcd.print("A");

  lcd.setCursor(0, 1);
  lcd.print("P:");
  lcd.print(charge_power, 2);
  lcd.print("W");
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);

  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);

  pinMode(LED_POWER, OUTPUT);
  pinMode(LED_BLUE,  OUTPUT);
  pinMode(LED_WIFI,  OUTPUT);

  // All LEDs off until mode is determined in loop()
  digitalWrite(LED_POWER, LOW);
  digitalWrite(LED_BLUE,  LOW);
  digitalWrite(LED_WIFI,  LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.print("Booting...");

  ina219.begin();

  scale1.begin(HX1_DT, HX1_SCK);
  scale2.begin(HX2_DT, HX2_SCK);

  // Scale factors for local debug weight display only
  scale1.set_scale(SCALE1);
  scale2.set_scale(SCALE2);

  scale1.tare();
  scale2.tare();

  Serial.println("[Setup] Connecting to WiFi: " + String(ssid));
  WiFi.begin(ssid, password);
  maintainWiFi();

  lcd.clear();
  lcd.print("WiFi OK");
  delay(1000);

  // Send an initial reading on boot
  readAllSensors();
  sendData("boot");
}

/* ================= LOOP ================= */
void loop() {

  maintainWiFi();

  float current  = ina219.getCurrent_mA() / 1000.0f;
  bool  solarMode = (current > 0.1f);

  gas1 = analogRead(MQ1_AO);
  gas2 = analogRead(MQ2_AO);
  bool gasDetected = (gas1 >= GAS_THRESHOLD || gas2 >= GAS_THRESHOLD);

  /* -------- SOLAR MODE: all hardware on, send every 60 s -------- */
  if (solarMode) {
    digitalWrite(LED_POWER, HIGH); // Green solid
    digitalWrite(LED_WIFI,  HIGH); // Yellow solid
    digitalWrite(LED_BLUE,  HIGH); // Blue solid
    lcd.backlight();

    if (gasDetected) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("!! GAS ALERT !!");

      delay(5000); // 5 s hardware stabilization before reading

      readAllSensors();
      sendData("gas_detected");

      while (gasDetected) {
        delay(5000);
        gas1 = analogRead(MQ1_AO);
        gas2 = analogRead(MQ2_AO);
        gasDetected = (gas1 >= GAS_THRESHOLD || gas2 >= GAS_THRESHOLD);
      }

      readAllSensors();
      sendData("gas_normal");
    }

    if (millis() - solarTimer >= 60000UL) {
      readAllSensors();
      sendData();
      solarTimer = millis();
    }
  }

  /* -------- IDLE MODE: gas monitoring only, send every 10 min -------- */
  else {
    digitalWrite(LED_POWER, LOW); // All LEDs off
    digitalWrite(LED_WIFI,  LOW);
    digitalWrite(LED_BLUE,  LOW);
    lcd.noBacklight();

    if (gasDetected) {
      delay(5000); // 5 s hardware stabilization (no LCD/LED in idle mode)

      readAllSensors();
      sendData("gas_detected");

      while (gasDetected) {
        delay(5000);
        gas1 = analogRead(MQ1_AO);
        gas2 = analogRead(MQ2_AO);
        gasDetected = (gas1 >= GAS_THRESHOLD || gas2 >= GAS_THRESHOLD);
      }

      readAllSensors();
      sendData("gas_normal");
    }

    if (millis() - idleTimer >= 600000UL) { // 10 minutes
      readAllSensors();
      sendData();
      idleTimer = millis();
    }
  }
}
