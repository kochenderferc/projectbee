#include "Arduino.h"
#include <Wire.h>

// ─── TEST CONFIGURATION ──────────────────────────────────────────────────────
#define TEST_MODE true  // set to false to use real sensors

// Pick one scenario to test:
//   0 = all values in range      → expect: OK
//   1 = temperature out of range → expect: TEMP ALERT
//   2 = humidity out of range    → expect: HUMIDITY ALERT
//   3 = PM2.5 out of range       → expect: AIR QUALITY ALERT
//   4 = vibration out of range   → expect: ACTIVITY ALERT
//   5 = all values out of range  → expect: all four alerts
int TEST_SCENARIO = 5;
// ─────────────────────────────────────────────────────────────────────────────

// all baseline values we are comparing against
// base temp is between 94-96 F; allowed tolerance range
float BASE_TEMP_C = 35.0;
float TEMP_TOLERANCE = 1.5;

// base humidity is optimal between 50-60%; allowed tolerance range
float BASE_HUMIDITY = 55.0;
float HUMIDITY_TOLERANCE = 5;

// particulate matter aiming for 0-1mg/m^3; conversion according to sensor
float BASE_PM25 = 200.0;
float PM25_TOLERANCE = 50.0;

// vibration at higher frequency range; 150-550 hz
// note: this is most likely to need to be calibrated
float BASE_VIBRATION = 350.0;
float VIBRATION_TOLERANCE = 200.0;

// SEN54 I2C
static const uint8_t SEN54_ADDR = 0x69;
static const uint8_t CMD_READ[] = {0x03, 0x00};
static const uint8_t CMD_START[] = {0x00, 0x21};

// sensor data
uint16_t pm25 = 0;
uint16_t humidity = 0;
uint16_t temperature = 0;

// piezo signal; vibration
const int PIEZO_PIN = A0;
float vibrationFiltered = 0;
float alpha = 0.2;

// ─── TEST SCENARIOS ───────────────────────────────────────────────────────────
struct TestCase {
  uint16_t pm25_raw;        // raw pm25 value
  uint16_t humidity_raw;    // raw humidity (divided by 100.0)
  uint16_t temperature_raw; // raw temperature (divided by 200.0)
  float vibration;          // already-filtered vibration value
  const char* label;
};

TestCase scenarios[] = {
  // pm25   humid  temp   vibration  label
  {  200,   5500,  7000,  350.0,   "All IN range"           },  // 0
  {  200,   5500,  8200,  350.0,   "TEMP out of range"      },  // 1: 41.0°C
  {  200,   3000,  7000,  350.0,   "HUMIDITY out of range"  },  // 2: 30%
  {  400,   5500,  7000,  350.0,   "PM2.5 out of range"     },  // 3
  {  200,   5500,  7000,  50.0,    "VIBRATION out of range" },  // 4
  {  400,   3000,  8200,  50.0,    "ALL out of range"       },  // 5
};
// ─────────────────────────────────────────────────────────────────────────────

// CRC checker
bool checkCRC(uint8_t* data) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < 2; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++)
      crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
  }
  return (crc == data[2]);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("Beehive Monitoring System — Baseline Mode");

  if (!TEST_MODE) {
    Wire.begin();
    delay(100);
    Wire.beginTransmission(SEN54_ADDR);
    Wire.write(CMD_START, 2);
    Wire.endTransmission();
    delay(1000);
  }

  Serial.println("PM2.5,TempC,Humidity,Vibration,Status");
}

void loop() {
  if (TEST_MODE) {
    injectTestValues();
  } else {
    readSEN54();
    readPiezo();
  }

  checkAgainstBaseline();
  // change this to measure every 30 minutes (1,800,000 as the delay value)
  delay(500);
}

void injectTestValues() {
  TestCase& t = scenarios[TEST_SCENARIO];
  pm25              = t.pm25_raw;
  humidity          = t.humidity_raw;
  temperature       = t.temperature_raw;
  vibrationFiltered = t.vibration;

  Serial.print("[TEST] Scenario ");
  Serial.print(TEST_SCENARIO);
  Serial.print(" — ");
  Serial.println(t.label);
}

void readSEN54() {
  Wire.beginTransmission(SEN54_ADDR);
  Wire.write(CMD_READ, 2);
  if (Wire.endTransmission() != 0) return;

  delay(50);

  uint8_t data[24];
  Wire.requestFrom((uint8_t)SEN54_ADDR, (uint8_t)24);
  if (Wire.available() < 24) return;
  for (int i = 0; i < 24; i++) data[i] = Wire.read();

  for (int i = 0; i < 24; i += 3)
    if (!checkCRC(&data[i])) return;

  pm25        = (data[6] << 8)  | data[7];
  humidity    = (data[12] << 8) | data[13];
  temperature = (data[14] << 8) | data[15];
}

void readPiezo() {
  int raw = analogRead(PIEZO_PIN);
  int centered = raw - 2048;
  vibrationFiltered = (alpha * abs(centered)) + ((1.0 - alpha) * vibrationFiltered);
}

void checkAgainstBaseline() {
  float tempC = temperature / 200.0;
  float humid = humidity / 100.0;

  bool warn = false;
  String status = "OK";

  if (abs(tempC - BASE_TEMP_C) > TEMP_TOLERANCE) {
    status = "TEMP ALERT";
    warn = true;
  }

  if (abs(humid - BASE_HUMIDITY) > HUMIDITY_TOLERANCE) {
    status += " HUMIDITY ALERT";
    warn = true;
  }

  if (abs(pm25 - BASE_PM25) > PM25_TOLERANCE) {
    status += " AIR QUALITY ALERT";
    warn = true;
  }

  if (abs(vibrationFiltered - BASE_VIBRATION) > VIBRATION_TOLERANCE) {
    status += " ACTIVITY ALERT";
    warn = true;
  }

  // CSV Data Output
  Serial.print(pm25);
  Serial.print(",");
  Serial.print(tempC, 2);
  Serial.print(",");
  Serial.print(humid, 2);
  Serial.print(",");
  Serial.print(vibrationFiltered, 1);
  Serial.print(",");
  Serial.println(status);

  if (warn) {
    Serial.println("⚠ Hive conditions abnormal! Inspect colony!");
  }
}