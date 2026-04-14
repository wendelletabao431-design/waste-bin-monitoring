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
#define SENSOR_WAKE_STABILIZE_MS 3000UL
#define GAS_CONFIRM_STABILIZE_MS 5000UL

/* ================= OBJECTS ================= */
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_INA219    ina219;
HX711 scale1;
HX711 scale2;

/* ================= TIMERS ================= */
unsigned long solarTimer = 0;
unsigned long idleTimer  = 0;
bool bootSendPending     = true;
bool sensorsAwake        = false;
bool lastSolarMode       = true;

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
void sleepWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(true);
  }
  WiFi.mode(WIFI_OFF);
}

bool maintainWiFi(unsigned long timeoutMs = 15000) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Reconnecting...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.begin(ssid, password);

    unsigned long startedAt = millis();

    // Blink LED_WIFI while connecting; main loop sets solid/off per mode after this
    while (WiFi.status() != WL_CONNECTED) {
      digitalWrite(LED_WIFI, HIGH); delay(200);
      digitalWrite(LED_WIFI, LOW);  delay(200);

      if (millis() - startedAt >= timeoutMs) {
        Serial.println("[WiFi] Connect timeout");
        return false;
      }
    }

    Serial.println("[WiFi] Connected: " + WiFi.localIP().toString());
  }

  return true;
}

/* ================= RESET REASON ================= */
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

/* ================= POWER / WAKE ================= */
void wakeSensors(const char* reason, unsigned long stabilizeMs = SENSOR_WAKE_STABILIZE_MS) {
  if (!sensorsAwake) {
    Serial.printf("[Wake] Sensors ON for %s\n", reason);
    scale1.power_up();
    scale2.power_up();
    sensorsAwake = true;
  }

  if (stabilizeMs > 0) {
    Serial.printf("[Wake] Stabilizing for %lu ms\n", stabilizeMs);
    delay(stabilizeMs);

    // Discard a few HX711 samples right after wake so the next read is steadier.
    scale1.get_value(3);
    scale2.get_value(3);
  }
}

void sleepSensors() {
  if (!sensorsAwake) {
    return;
  }

  Serial.println("[Sleep] Sensors idle");
  scale1.power_down();
  scale2.power_down();
  sensorsAwake = false;
}

bool quickGasDetected() {
  gas1 = analogRead(MQ1_AO);
  gas2 = analogRead(MQ2_AO);
  return (gas1 >= GAS_THRESHOLD || gas2 >= GAS_THRESHOLD);
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

bool confirmGasState(const char* reason) {
  wakeSensors(reason, GAS_CONFIRM_STABILIZE_MS);
  readAllSensors();
  return (gas1 >= GAS_THRESHOLD || gas2 >= GAS_THRESHOLD);
}

/* ================= SEND DATA ================= */
void sendData(String eventTag = "") {

  if (!maintainWiFi()) {
    Serial.println("[HTTP] Skipping send because WiFi is unavailable");
    return;
  }

  // Blink blue LED during HTTP send — turn LOW for duration of POST.
  // In solar mode this creates a visible blink; in idle mode it is already LOW.
  // Main loop restores correct state per mode on next iteration.
  digitalWrite(LED_BLUE, LOW);

  WiFiClientSecure client;
  client.setInsecure(); // Skip TLS cert verification for Railway

  HTTPClient http;
  const char* headerKeys[] = {"Location", "Content-Type"};
  http.collectHeaders(headerKeys, 2);
  http.begin(client, serverURL);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
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

  if (httpCode == 200 || httpCode == 201) {
    String contentType = http.header("Content-Type");
    Serial.printf("[HTTP] POST OK (%d), content-type=%s\n", httpCode, contentType.c_str());
  } else if (httpCode > 0) {
    String location = http.header("Location");
    String contentType = http.header("Content-Type");
    Serial.printf("[HTTP] POST FAILED (%d), location=%s, content-type=%s\n",
      httpCode, location.c_str(), contentType.c_str());
  } else {
    Serial.printf("[HTTP] POST ERROR (%d): %s\n", httpCode, http.errorToString(httpCode).c_str());
  }

  http.end();

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
  delay(200);

  esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.printf("[Boot] Reset reason: %d (%s)\n", resetReason, resetReasonToString(resetReason));

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
  sensorsAwake = true;

  Serial.println("[Setup] Connecting to WiFi: " + String(ssid));
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  maintainWiFi();

  lcd.clear();
  lcd.print(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi Retry");
  delay(1000);

  // Read sensors at boot, but defer the first POST until loop()
  // so we avoid the heaviest network/TLS work during startup.
  readAllSensors();
}

/* ================= LOOP ================= */
void loop() {

  float current  = ina219.getCurrent_mA() / 1000.0f;
  bool  solarMode = (current > 0.1f);
  bool  gasDetected = quickGasDetected();

  if (bootSendPending && millis() >= 5000UL) {
    sendData("boot");
    bootSendPending = false;
    solarTimer = millis();
    idleTimer = millis();
  }

  if (solarMode != lastSolarMode) {
    if (solarMode) {
      wakeSensors("solar mode", SENSOR_WAKE_STABILIZE_MS);
    } else {
      sleepSensors();
      sleepWiFi();
    }
    lastSolarMode = solarMode;
  }

  /* -------- SOLAR MODE: all hardware on, send every 60 s -------- */
  if (solarMode) {
    wakeSensors("solar mode", 0);
    maintainWiFi();

    digitalWrite(LED_POWER, HIGH); // Green solid
    digitalWrite(LED_WIFI,  HIGH); // Yellow solid
    digitalWrite(LED_BLUE,  HIGH); // Blue solid
    lcd.backlight();

    if (gasDetected) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("!! GAS ALERT !!");

      if (confirmGasState("gas_detected")) {
        sendData("gas_detected");

        while (quickGasDetected()) {
          delay(5000);
        }

        if (!confirmGasState("gas_normal")) {
          sendData("gas_normal");
        }
      } else {
        Serial.println("[Gas] Cleared before confirmed gas_detected send");
      }
    }

    if (millis() - solarTimer >= 60000UL) {
      wakeSensors("solar sample", SENSOR_WAKE_STABILIZE_MS);
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
      if (confirmGasState("gas_detected")) {
        sendData("gas_detected");

        while (quickGasDetected()) {
          delay(5000);
        }

        if (!confirmGasState("gas_normal")) {
          sendData("gas_normal");
        }
      } else {
        Serial.println("[Gas] Cleared before confirmed gas_detected send");
      }

      sleepSensors();
      sleepWiFi();
    }

    if (millis() - idleTimer >= 600000UL) { // 10 minutes
      wakeSensors("idle sample", SENSOR_WAKE_STABILIZE_MS);
      readAllSensors();
      sendData();
      idleTimer = millis();
      sleepSensors();
      sleepWiFi();
    }
  }
}
