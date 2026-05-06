# Beehive Monitor

## Overview
Logs temperature, humidity, PM2.5, and vibration from a beehive every 30 minutes.
Readings are stored in the Arduino Mega's EEPROM (up to 315 records ≈ 6.5 days).
When you plug the Mega into a computer, it automatically dumps all stored readings
as a CSV over Serial, then clears the buffer ready for the next run.

## Files
* 'beehive_monitor.ino' is the Arduino code; uploaded to Arduino Mega breadboard
* 'beehive_logger.py' is the Python script; run on computer after device connection to download the CSV

## Initial setup

### 1. Install Arduino IDE
Download from https://www.arduino.cc/en/software

### 2. Upload the sketch
1. Open `beehive_monitor.ino` in Arduino IDE
2. Set board: **Tools → Board → Arduino Mega 2560**
3. Set port: **Tools → Port → (your Mega's port)**
4. Click **Upload**

### 3. Install Python dependency
```bash
pip install pyserial
```
## Firmware/Software Testing

Check the top of `beehive_monitor.ino`:

```cpp
#define TEST_MODE true   // set false for real sensors
int TEST_SCENARIO = 5;   // 0–5, see table below
```

| `TEST_SCENARIO` | Expected `alerts` output |
|---|---|
| 0 | `OK` |
| 1 | `TEMP` |
| 2 | `HUMIDITY` |
| 3 | `AIR_QUALITY` |
| 4 | `ACTIVITY` |
| 5 | `TEMP\|HUMIDITY\|AIR_QUALITY\|ACTIVITY` |

In test mode readings come in every **0.5 seconds**, so the buffer fills in under
3 minutes. Change `TEST_SCENARIO`, re-upload, and reconnect to verify each alert.

When completed testing, set `TEST_MODE false` and change the delay:
```cpp
delay(1800000);  // 30 minutes
```
## Collecting data

### Find your port

**Mac**
```bash
ls /dev/tty.*
# look for something like /dev/tty.usbmodem1401
```

**Windows** — open Device Manager → Ports (COM & LPT)
Look for *USB Serial Device (COMx)*.

### Run the logger

**Mac / Linux**
```bash
cd /path/to/folder/containing/beehive_logger.py
python3 beehive_logger.py /dev/tty.usbmodem1401
```

**Windows**
```bash
cd C:\path\to\folder\containing\beehive_logger.py
python beehive_logger.py COM3
```

Replace the port with your actual port name.

The script saves a timestamped CSV file in the same folder, e.g.:
```
beehive_20260423_143022.csv
```

### CSV columns

| Column | Description |
|---|---|
| `timestamp` | Wall-clock time logged by the Python script |
| `pm25` | Raw PM2.5 value from SEN54 |
| `temp_c` | Temperature in °C |
| `humidity` | Relative humidity % |
| `vibration` | Filtered piezo reading |
| `alerts` | `OK` or pipe-separated alert codes |
| `warn` | `1` if any alert fired, `0` if all clear |

## Baseline thresholds

Adjust these at the top of `beehive_monitor.ino` to suit your hive:

| Parameter | Base value | Tolerance |
|---|---|---|
| Temperature | 35.0 °C | ± 1.5 °C |
| Humidity | 55 % | ± 5 % |
| PM2.5 | 200 (raw) | ± 50 |
| Vibration | 350 (raw) | ± 200 |

Our given baseline thresholds were determined through literature review. 

## Storage limits

The Mega has 4096 bytes of EEPROM. Each record uses 13 bytes, giving
**315 records maximum**. At 30-minute intervals that is ~6.5 days.

Once full the buffer wraps and overwrites the oldest records, so you always
have the most recent 6.5 days of data. Dump regularly to avoid losing readings.
