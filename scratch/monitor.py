import serial
import sys
import time

def main():
    print("Starting serial monitor on COM3...", flush=True)
    try:
        s = serial.Serial('COM3', 115200, timeout=1)
    except Exception as e:
        print(f"Error opening serial port: {e}", flush=True)
        sys.exit(1)
        
    s.dtr = False
    s.rts = False
    
    print("Monitoring serial output (filtering out LVGL rendering ticks)...", flush=True)
    try:
        while True:
            line = s.readline()
            if line:
                line_str = line.decode('utf-8', errors='ignore')
                if 'LVGL_TASK' not in line_str and 'FLUSH' not in line_str:
                    sys.stdout.write(line_str)
                    sys.stdout.flush()
    except KeyboardInterrupt:
        print("Monitor stopped.", flush=True)
    finally:
        s.close()

if __name__ == '__main__':
    main()
