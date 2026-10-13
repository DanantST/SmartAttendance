import serial
import time
import sys

def main():
    port = 'COM3'
    baud = 115200
    print(f"Connecting to {port} at {baud}...")
    log_file = "scratch/monitor_log_com3.txt"
    
    with open(log_file, "w", encoding="utf-8") as f_out:
        try:
            s = serial.Serial(port, baud, timeout=0.1)
            # Reset chip
            s.setDTR(False)
            s.setRTS(True)
            time.sleep(0.1)
            s.setRTS(False)
            print(f"Connected to {port} and triggered reset. Reading serial (20 seconds)...")
            start = time.time()
            while time.time() - start < 20:
                line = s.readline()
                if line:
                    text = line.decode('utf-8', errors='replace')
                    sys.stdout.write(text)
                    sys.stdout.flush()
                    f_out.write(text)
                    f_out.flush()
            s.close()
        except Exception as e:
            print(f"Serial error: {e}")

if __name__ == '__main__':
    main()
