#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_INA219.h>
#include "HX711.h"

/* WIFI */

const char* ssid = "TABAO_FAM";
const char* password = "JOAN062199";
const char* serverURL = "https://smart-trash-bin-production.up.railway.app/api/bin-data";
const char* fallbackDeviceID = "ESP32_SMART_BIN";

/* LED */

#define LED_POWER 27
#define LED_BLUE 2
#define LED_WIFI 4

/* I2C */

#define SDA_PIN 13
#define SCL_PIN 14

/* ULTRASONIC */

#define TRIG1 5
#define ECHO1 18
#define TRIG2 17
#define ECHO2 16

/* LOAD CELLS */

#define HX1_DT 19
#define HX1_SCK 23
#define HX2_DT 21
#define HX2_SCK 22

/* GAS */

#define MQ1_AO 34
#define MQ2_AO 32

/* BATTERY */

#define BATTERY_PIN 33

/* CALIBRATION */

#define BIN1_EMPTY 58.7
#define BIN2_EMPTY 48.3
#define BIN_FULL 10

#define SCALE1 90.4
#define SCALE2 92.6
#define RAW_EMPTY_BIN1 514375L
#define RAW_EMPTY_BIN2 -480493L
#define MAX_WEIGHT_KG 20.0

#define GAS_THRESHOLD 500

/* OBJECTS */

LiquidCrystal_I2C lcd(0x27,16,2);
Adafruit_INA219 ina219;
HX711 scale1;
HX711 scale2;

/* TIMERS */

unsigned long solarTimer = 0;
unsigned long batteryTimer = 0;

/* RAW SENSOR STATE */

float bin1DistanceCm = -1;
float bin2DistanceCm = -1;
float bin1Level = 0;
float bin2Level = 0;
long bin1WeightRaw = 0;
long bin2WeightRaw = 0;
float bin1WeightKg = 0;
float bin2WeightKg = 0;
int gas1 = 0;
int gas2 = 0;
int batteryAdc = 0;

/* ================= ULTRASONIC FILTER ================= */

float readUltrasonicFiltered(int trig,int echo){

  float total = 0;
  int count = 0;

  for(int i=0;i<5;i++){

    digitalWrite(trig,LOW);
    delayMicroseconds(2);

    digitalWrite(trig,HIGH);
    delayMicroseconds(10);
    digitalWrite(trig,LOW);

    long duration = pulseIn(echo,HIGH,30000);

    if(duration>0){

      float dist = duration*0.0343/2;

      total += dist;
      count++;

    }

    delay(20);

  }

  if(count==0) return -1;

  return total/count;

}

/* ================= BIN LEVEL ================= */

float getPercent(float dist,float empty){

  if(dist < 0){
    return 0;
  }

  float level = (empty-dist)/(empty-BIN_FULL)*100;

  return constrain(level,0,100);

}

float getWeightKg(long rawValue,long rawEmpty,float scalePerGram){

  if(scalePerGram == 0){
    return 0;
  }

  float weightKg = ((rawValue - rawEmpty) / scalePerGram) / 1000.0;

  return constrain(weightKg,0,MAX_WEIGHT_KG);

}

String getDeviceID(){

  String deviceID = WiFi.macAddress();

  if(deviceID.length() == 0 || deviceID == "00:00:00:00:00:00"){
    return String(fallbackDeviceID);
  }

  return deviceID;

}

/* ================= WIFI AUTO RECONNECT ================= */

void maintainWiFi(){

  if(WiFi.status()!=WL_CONNECTED){

    digitalWrite(LED_WIFI,LOW);

    WiFi.disconnect();
    WiFi.begin(ssid,password);

    unsigned long start = millis();

    while(WiFi.status()!=WL_CONNECTED && millis()-start<10000){

      digitalWrite(LED_WIFI,HIGH);
      delay(200);

      digitalWrite(LED_WIFI,LOW);
      delay(200);

    }

  }

  if(WiFi.status()==WL_CONNECTED){

    digitalWrite(LED_WIFI,HIGH);

  }

}

void readAllSensors(){

  bin1DistanceCm = readUltrasonicFiltered(TRIG1,ECHO1);
  bin2DistanceCm = readUltrasonicFiltered(TRIG2,ECHO2);

  bin1Level = getPercent(bin1DistanceCm,BIN1_EMPTY);
  bin2Level = getPercent(bin2DistanceCm,BIN2_EMPTY);

  bin1WeightRaw = scale1.read_average(10);
  bin2WeightRaw = scale2.read_average(10);

  bin1WeightKg = getWeightKg(bin1WeightRaw,RAW_EMPTY_BIN1,SCALE1);
  bin2WeightKg = getWeightKg(bin2WeightRaw,RAW_EMPTY_BIN2,SCALE2);

  gas1 = analogRead(MQ1_AO);
  gas2 = analogRead(MQ2_AO);
  batteryAdc = analogRead(BATTERY_PIN);

}

/* ================= SEND DATA ================= */

void sendData(String eventTag=""){

  maintainWiFi();

  if(WiFi.status()!=WL_CONNECTED){

    Serial.println("WiFi not connected. Skipping backend send.");
    return;

  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  http.setTimeout(10000);

  if(!http.begin(client,serverURL)){

    Serial.println("Failed to initialize HTTPS connection.");
    return;

  }

  http.addHeader("Content-Type","application/json");
  http.addHeader("Accept","application/json");

  String json="{";

  if(eventTag!=""){
    json+="\"event\":\""+eventTag+"\",";
  }

  json+="\"device_id\":\""+getDeviceID()+"\",";
  json+="\"battery_adc\":"+String(batteryAdc)+",";
  json+="\"bin_1\":{";
  json+="\"distance_cm\":"+String(bin1DistanceCm,1)+",";
  json+="\"hx711_raw\":"+String(bin1WeightRaw)+",";
  json+="\"mq_raw\":"+String(gas1);
  json+="},";
  json+="\"bin_2\":{";
  json+="\"distance_cm\":"+String(bin2DistanceCm,1)+",";
  json+="\"hx711_raw\":"+String(bin2WeightRaw)+",";
  json+="\"mq_raw\":"+String(gas2);
  json+="}";

  json+="}";

  Serial.println("Sending payload to backend:");
  Serial.println(json);

  int httpCode = http.POST(json);

  Serial.print("HTTP status: ");
  Serial.println(httpCode);

  if(httpCode > 0){

    String response = http.getString();
    Serial.println("Backend response:");
    Serial.println(response);

  } else {

    Serial.print("HTTP error: ");
    Serial.println(http.errorToString(httpCode));

  }

  http.end();

}

/* ================= SETUP ================= */

void setup(){

  Serial.begin(115200);

  pinMode(TRIG1,OUTPUT);
  pinMode(ECHO1,INPUT);

  pinMode(TRIG2,OUTPUT);
  pinMode(ECHO2,INPUT);

  pinMode(LED_POWER,OUTPUT);
  pinMode(LED_BLUE,OUTPUT);
  pinMode(LED_WIFI,OUTPUT);
  pinMode(BATTERY_PIN,INPUT);

  digitalWrite(LED_POWER,HIGH);

  Wire.begin(SDA_PIN,SCL_PIN);

  lcd.init();
  lcd.backlight();

  ina219.begin();

  scale1.begin(HX1_DT,HX1_SCK);
  scale2.begin(HX2_DT,HX2_SCK);

  scale1.set_scale(SCALE1);
  scale2.set_scale(SCALE2);

  scale1.tare();
  scale2.tare();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid,password);

}

/* ================= LOOP ================= */

void loop(){

  float solarCurrent = ina219.getCurrent_mA()/1000.0;

  bool solarMode = solarCurrent > 0.1;

  gas1 = analogRead(MQ1_AO);
  gas2 = analogRead(MQ2_AO);

  bool gasDetected = (gas1>=GAS_THRESHOLD || gas2>=GAS_THRESHOLD);

  /* GAS ALERT */

  if(gasDetected){

    lcd.backlight();
    lcd.clear();
    lcd.print("Flammable Gas");

    delay(5000);

    readAllSensors();

    sendData("gas_detected");

    while(gasDetected){

      delay(5000);

      gas1 = analogRead(MQ1_AO);
      gas2 = analogRead(MQ2_AO);

      gasDetected = (gas1>=GAS_THRESHOLD || gas2>=GAS_THRESHOLD);

    }

    readAllSensors();
    sendData("gas_normal");

  }

  /* SOLAR MODE */

  if(solarMode){

    digitalWrite(LED_BLUE,HIGH);

    lcd.backlight();

    if(millis()-solarTimer >= 60000){

      readAllSensors();

      Serial.print("BIN 1 Distance (cm): ");
      Serial.println(bin1DistanceCm);
      Serial.print("BIN 1 Fill (%): ");
      Serial.println(bin1Level);
      Serial.print("BIN 1 Weight Raw: ");
      Serial.println(bin1WeightRaw);
      Serial.print("BIN 1 Weight (kg): ");
      Serial.println(bin1WeightKg,2);

      Serial.print("BIN 2 Distance (cm): ");
      Serial.println(bin2DistanceCm);
      Serial.print("BIN 2 Fill (%): ");
      Serial.println(bin2Level);
      Serial.print("BIN 2 Weight Raw: ");
      Serial.println(bin2WeightRaw);
      Serial.print("BIN 2 Weight (kg): ");
      Serial.println(bin2WeightKg,2);

      lcd.clear();
      lcd.print("Data Sent");

      sendData();

      solarTimer = millis();

    }

  }

  /* BATTERY MODE */

  else{

    digitalWrite(LED_BLUE,LOW);

    lcd.noBacklight();

    if(millis()-batteryTimer >= 1200000){

      lcd.backlight();
      lcd.clear();
      lcd.print("Data Sent");

      readAllSensors();

      Serial.print("BIN 1 Distance (cm): ");
      Serial.println(bin1DistanceCm);
      Serial.print("BIN 1 Fill (%): ");
      Serial.println(bin1Level);
      Serial.print("BIN 1 Weight Raw: ");
      Serial.println(bin1WeightRaw);
      Serial.print("BIN 1 Weight (kg): ");
      Serial.println(bin1WeightKg,2);

      Serial.print("BIN 2 Distance (cm): ");
      Serial.println(bin2DistanceCm);
      Serial.print("BIN 2 Fill (%): ");
      Serial.println(bin2Level);
      Serial.print("BIN 2 Weight Raw: ");
      Serial.println(bin2WeightRaw);
      Serial.print("BIN 2 Weight (kg): ");
      Serial.println(bin2WeightKg,2);

      sendData();

      delay(3000);

      lcd.noBacklight();

      batteryTimer = millis();

    }

  }

}
