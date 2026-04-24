"""
beehive_logger.py
─────────────────
Reads the beehive monitor's serial output and appends each data row to a CSV.
Lines beginning with '#' (comments / test labels) are printed to the console
but never written to the CSV.

Usage:
    pip install pyserial
    python beehive_logger.py                     # uses defaults below
    python beehive_logger.py COM3 115200         # Windows
    python beehive_logger.py /dev/ttyUSB0 115200 # Linux / Mac
"""

import sys
import csv
import serial
import datetime
from pathlib import Path

# ── Configuration ─────────────────────────────────────────────────────────────
PORT      = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
BAUD      = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
# Output file — a new file is created for each run (timestamped)
OUT_DIR   = Path(".")
# ──────────────────────────────────────────────────────────────────────────────

CSV_HEADER = ["timestamp", "pm25", "temp_c", "humidity", "vibration", "alerts", "warn"]

def main():
    run_start = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path  = OUT_DIR / f"beehive_{run_start}.csv"

    print(f"Logging to: {csv_path}")
    print(f"Port: {PORT} @ {BAUD} baud")
    print("Press Ctrl+C to stop.\n")

    with serial.Serial(PORT, BAUD, timeout=2) as ser, \
         open(csv_path, "w", newline="", encoding="utf-8") as fh:

        writer = csv.writer(fh)
        writer.writerow(CSV_HEADER)
        fh.flush()

        header_written = False

        while True:
            try:
                raw = ser.readline()
            except serial.SerialException as e:
                print(f"Serial error: {e}")
                break

            if not raw:
                continue

            line = raw.decode("utf-8", errors="replace").strip()

            # Comment / debug lines — show in console, skip for CSV
            if line.startswith("#") or line == "":
                print(line)
                continue

            # Skip the Arduino-side header row (it starts with "timestamp")
            if line.startswith("timestamp"):
                if not header_written:
                    print(f"[header] {line}")
                    header_written = True
                continue

            # Parse the comma-separated data row
            parts = line.split(",")
            if len(parts) != 7:
                print(f"[skip — unexpected columns] {line}")
                continue

            # Replace the on-device millis() timestamp with a real wall-clock time
            wall_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            parts[0]  = wall_time

            writer.writerow(parts)
            fh.flush()          # write immediately so the file is always up to date

            # Pretty-print to console
            _, pm25, temp, humid, vib, alerts, warn = parts
            flag = " ⚠  ALERT" if warn == "1" else ""
            print(f"{wall_time}  PM2.5={pm25}  T={temp}°C  H={humid}%  "
                  f"Vib={vib}  [{alerts}]{flag}")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nLogger stopped.")
