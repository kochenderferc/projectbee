#include "Arduino.h"
#include <Wire.h>
#include <EEPROM.h>

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

// ─── EEPROM LAYOUT ───────────────────────────────────────────────────────────
// Each record = 13 bytes:
//   millis    uint32_t  4 bytes
//   pm25      uint16_t  2 bytes
//   temp×100  int16_t   2 bytes
//   humid×100 int16_t   2 bytes
//   vib×10    int16_t   2 bytes
//   alerts    uint8_t   1 byte  (bitmask)
//
// Header (5 bytes):
//   0x00  magic  uint8_t   (0xBE = initialised)
//   0x01  head   uint16_t  (next write slot, 0-based)
//   0x03  count  uint16_t  (records stored, max = MAX_RECORDS)
//
// Mega EEPROM = 4096 bytes → 315 records ≈ 6.5 days at 30-min intervals
#define EEPROM_SIZE   4096
#define RECORD_SIZE   13
#define HEADER_SIZE   5
#define MAX_RECORDS   ((EEPROM_SIZE - HEADER_SIZE) / RECORD_SIZE)  // 315
#define MAGIC_BYTE    0xBE
#define MAGIC_ADDR    0
#define HEAD_ADDR     1
#define COUNT_ADDR    3

// Alert bitmask flags
#define ALERT_TEMP     0x01
#define ALERT_HUMID    0x02
#define ALERT_AIR      0x04
#define ALERT_ACTIVITY 0x08
// ─────────────────────────────────────────────────────────────────────────────

// ─── BASELINE VALUES ─────────────────────────────────────────────────────────
float BASE_TEMP_C         = 35.0;
float TEMP_TOLERANCE      = 1.5;
float BASE_HUMIDITY       = 55.0;
float HUMIDITY_TOLERANCE  = 5.0;
float BASE_PM25           = 200.0;
float PM25_TOLERANCE      = 50.0;
float BASE_VIBRATION      = 350.0;
float VIBRATION_TOLERANCE = 200.0;
// ─────────────────────────────────────────────────────────────────────────────

// SEN54 I2C
static const uint8_t SEN54_ADDR  = 0x69;
static const uint8_t CMD_READ[]  = {0x03, 0x00};
static const uint8_t CMD_START[] = {0x00, 0x21};

// Sensor globals
uint16_t pm25        = 0;
uint16_t humidity    = 0;
uint16_t temperature = 0;

// Piezo / vibration
const int PIEZO_PIN         = A0;
float     vibrationFiltered = 0;
float     alpha             = 0.2;

// ─── TEST SCENARIOS ───────────────────────────────────────────────────────────
struct TestCase {
  uint16_t pm25_raw;
  uint16_t humidity_raw;
  uint16_t temperature_raw;
  float    vibration;
};

TestCase scenarios[] = {
  {  200, 5500, 7000, 350.0 },  // 0 — all in range
  {  200, 5500, 8200, 350.0 },  // 1 — temp out
  {  200, 3000, 7000, 350.0 },  // 2 — humidity out
  {  400, 5500, 7000, 350.0 },  // 3 — PM2.5 out
  {  200, 5500, 7000,  50.0 },  // 4 — vibration out
  {  400, 3000, 8200,  50.0 },  // 5 — all out
};
// ─────────────────────────────────────────────────────────────────────────────

// ─── EEPROM HELPERS ───────────────────────────────────────────────────────────
uint16_t eepromReadU16(int addr) {
  return ((uint16_t)EEPROM.read(addr) << 8) | EEPROM.read(addr + 1);
}

void eepromWriteU16(int addr, uint16_t val) {
  EEPROM.write(addr,      (val >> 8) & 0xFF);
  EEPROM.write(addr + 1,   val       & 0xFF);
}

int recordAddr(uint16_t slot) {
  return HEADER_SIZE + (int)(slot % MAX_RECORDS) * RECORD_SIZE;
}

void writeRecord(uint32_t ts, uint16_t p, float tempC,
                 float humid, float vib, uint8_t alertMask) {
  uint16_t head  = eepromReadU16(HEAD_ADDR);
  uint16_t count = eepromReadU16(COUNT_ADDR);

  int addr = recordAddr(head);

  EEPROM.write(addr + 0, (ts >> 24) & 0xFF);
  EEPROM.write(addr + 1, (ts >> 16) & 0xFF);
  EEPROM.write(addr + 2, (ts >>  8) & 0xFF);
  EEPROM.write(addr + 3,  ts        & 0xFF);

  EEPROM.write(addr + 4, (p >> 8) & 0xFF);
  EEPROM.write(addr + 5,  p       & 0xFF);

  int16_t t16 = (int16_t)(tempC * 100.0);
  EEPROM.write(addr + 6, (t16 >> 8) & 0xFF);
  EEPROM.write(addr + 7,  t16       & 0xFF);

  int16_t h16 = (int16_t)(humid * 100.0);
  EEPROM.write(addr + 8, (h16 >> 8) & 0xFF);
  EEPROM.write(addr + 9,  h16       & 0xFF);

  int16_t v16 = (int16_t)(vib * 10.0);
  EEPROM.write(addr + 10, (v16 >> 8) & 0xFF);
  EEPROM.write(addr + 11,  v16       & 0xFF);

  EEPROM.write(addr + 12, alertMask);

  head = (head + 1) % MAX_RECORDS;
  eepromWriteU16(HEAD_ADDR, head);
  if (count < MAX_RECORDS) eepromWriteU16(COUNT_ADDR, count + 1);
}

// ─── DUMP ─────────────────────────────────────────────────────────────────────
void alertsToString(uint8_t mask, char* buf) {
  buf[0] = '\0';
  if (mask == 0) { strcpy(buf, "OK"); return; }
  if (mask & ALERT_TEMP)     strcat(buf, "TEMP|");
  if (mask & ALERT_HUMID)    strcat(buf, "HUMIDITY|");
  if (mask & ALERT_AIR)      strcat(buf, "AIR_QUALITY|");
  if (mask & ALERT_ACTIVITY) strcat(buf, "ACTIVITY|");
  int len = strlen(buf);
  if (len > 0 && buf[len - 1] == '|') buf[len - 1] = '\0';
}

// Dumps exactly snapshotCount records starting from snapshotStart.
// These were captured before the USB connection, so no live data bleeds in.
void dumpSnapshot(uint16_t snapshotStart, uint16_t snapshotCount) {
  if (snapshotCount == 0) {
    Serial.println("#No records from battery session.");
    return;
  }

  Serial.println("#BEGIN_DUMP");
  Serial.println("millis,pm25,temp_c,humidity,vibration,alerts,warn");

  char alertBuf[48];

  for (uint16_t i = 0; i < snapshotCount; i++) {
    int addr = recordAddr((snapshotStart + i) % MAX_RECORDS);

    uint32_t ts = ((uint32_t)EEPROM.read(addr + 0) << 24) |
                  ((uint32_t)EEPROM.read(addr + 1) << 16) |
                  ((uint32_t)EEPROM.read(addr + 2) <<  8) |
                   (uint32_t)EEPROM.read(addr + 3);

    uint16_t p  = ((uint16_t)EEPROM.read(addr + 4) << 8) |
                               EEPROM.read(addr + 5);

    int16_t t16 = ((int16_t)EEPROM.read(addr + 6) << 8) |
                              EEPROM.read(addr + 7);

    int16_t h16 = ((int16_t)EEPROM.read(addr + 8) << 8) |
                              EEPROM.read(addr + 9);

    int16_t v16 = ((int16_t)EEPROM.read(addr + 10) << 8) |
                              EEPROM.read(addr + 11);

    uint8_t mask = EEPROM.read(addr + 12);

    alertsToString(mask, alertBuf);

    Serial.print(ts);              Serial.print(",");
    Serial.print(p);               Serial.print(",");
    Serial.print(t16 / 100.0, 2); Serial.print(",");
    Serial.print(h16 / 100.0, 2); Serial.print(",");
    Serial.print(v16 / 10.0,  1); Serial.print(",");
    Serial.print(alertBuf);        Serial.print(",");
    Serial.println(mask != 0 ? 1 : 0);
  }

  Serial.println("#END_DUMP");
  Serial.print("#Dumped ");
  Serial.print(snapshotCount);
  Serial.println(" battery-session records.");
}

// ─── CRC ──────────────────────────────────────────────────────────────────────
bool checkCRC(uint8_t* data) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < 2; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++)
      crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
  }
  return (crc == data[2]);
}

// ─── SETUP ────────────────────────────────────────────────────────────────────
void setup() {
  // Initialise EEPROM header on very first boot
  if (EEPROM.read(MAGIC_ADDR) != MAGIC_BYTE) {
    EEPROM.write(MAGIC_ADDR, MAGIC_BYTE);
    eepromWriteU16(HEAD_ADDR,  0);
    eepromWriteU16(COUNT_ADDR, 0);
  }

  // ── Snapshot the buffer state RIGHT NOW, before anything else runs ────────
  // These values represent only the data collected while on battery power.
  // Any records written after this point are from the current (USB) session
  // and will NOT be included in the dump.
  uint16_t snapshotCount = eepromReadU16(COUNT_ADDR);
  uint16_t snapshotHead  = eepromReadU16(HEAD_ADDR);
  uint16_t snapshotStart = (uint16_t)((snapshotHead - snapshotCount + MAX_RECORDS) % MAX_RECORDS);

  if (!TEST_MODE) {
    Wire.begin();
    delay(100);
    Wire.beginTransmission(SEN54_ADDR);
    Wire.write(CMD_START, 2);
    Wire.endTransmission();
    delay(1000);
  }

  // ── Wait for Serial, then dump only the battery-session snapshot ──────────
  Serial.begin(115200);
  unsigned long waitStart = millis();
  while (!Serial && (millis() - waitStart < 5000)) {
    delay(50);
  }

  if (Serial) {
    delay(60000); // give logger script time to open the port
    Serial.print("#Beehive Monitor online — ");
    Serial.print(snapshotCount);
    Serial.print("/");
    Serial.print(MAX_RECORDS);
    Serial.println(" battery-session records found. Dumping...");
    dumpSnapshot(snapshotStart, snapshotCount);
    Serial.println("#Live logging started. These readings are NOT saved to EEPROM.");
  }
}

// ─── MAIN LOOP ────────────────────────────────────────────────────────────────
void loop() {
  if (TEST_MODE) {
    injectTestValues();
  } else {
    readSEN54();
    readPiezo();
  }

  checkAgainstBaseline();
  // Change to 1800000 for 30-minute intervals in production
  delay(10000);
}

// ─── SENSOR READS ─────────────────────────────────────────────────────────────
void injectTestValues() {
  TestCase& t       = scenarios[TEST_SCENARIO];
  pm25              = t.pm25_raw;
  humidity          = t.humidity_raw;
  temperature       = t.temperature_raw;
  vibrationFiltered = t.vibration;
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

  pm25        = (data[6]  << 8) | data[7];
  humidity    = (data[12] << 8) | data[13];
  temperature = (data[14] << 8) | data[15];
}

void readPiezo() {
  int raw      = analogRead(PIEZO_PIN);
  int centered = raw - 2048;
  vibrationFiltered = (alpha * abs(centered)) + ((1.0 - alpha) * vibrationFiltered);
}

// ─── BASELINE CHECK & LOG ─────────────────────────────────────────────────────
void checkAgainstBaseline() {
  float tempC = temperature / 200.0;
  float humid = humidity    / 100.0;

  uint8_t alertMask = 0;
  if (abs(tempC - BASE_TEMP_C) > TEMP_TOLERANCE)                     alertMask |= ALERT_TEMP;
  if (abs(humid - BASE_HUMIDITY) > HUMIDITY_TOLERANCE)                alertMask |= ALERT_HUMID;
  if (abs((float)pm25 - BASE_PM25) > PM25_TOLERANCE)                 alertMask |= ALERT_AIR;
  if (abs(vibrationFiltered - BASE_VIBRATION) > VIBRATION_TOLERANCE) alertMask |= ALERT_ACTIVITY;

  // Only write to EEPROM when NOT connected to a computer.
  // When Serial is active we are plugged in — skip the write.
  if (!Serial) {
    writeRecord(millis(), pm25, tempC, humid, vibrationFiltered, alertMask);
  }

  // Echo live reading to Serial if connected (not saved)
  if (Serial) {
    char alertBuf[48];
    alertsToString(alertMask, alertBuf);

    Serial.print(millis());              Serial.print(",");
    Serial.print(pm25);                  Serial.print(",");
    Serial.print(tempC, 2);             Serial.print(",");
    Serial.print(humid, 2);             Serial.print(",");
    Serial.print(vibrationFiltered, 1); Serial.print(",");
    Serial.print(alertBuf);             Serial.print(",");
    Serial.println(alertMask != 0 ? 1 : 0);
  }
}
