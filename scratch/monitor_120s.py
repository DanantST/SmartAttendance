import serial
import time
import sys

PORT = "COM3"
BAUD = 115200
DURATION = 120  # seconds
LOG_PATH = "scratch/monitor_120s.log"

print(f"Starting 120-second serial monitor on {PORT} @ {BAUD} baud...")
sys.stdout.flush()

try:
    ser = serial.Serial(PORT, BAUD, timeout=1.0)
except Exception as e:
    print(f"Failed to open {PORT}: {e}")
    sys.exit(1)

t0 = time.time()
with open(LOG_PATH, "w", encoding="utf-8", errors="replace") as log_file:
    while time.time() - t0 < DURATION:
        try:
            line = ser.readline()
            if line:
                decoded = line.decode("utf-8", errors="replace")
                print(decoded, end="")
                sys.stdout.flush()
                log_file.write(decoded)
                log_file.flush()
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"Read error: {e}")
            break

ser.close()
elapsed = time.time() - t0
print(f"\n--- Monitor session complete ({elapsed:.1f}s recorded). Saved to {LOG_PATH} ---")
