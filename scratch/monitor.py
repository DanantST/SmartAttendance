import serial
import sys
import time

def main():
    print("Starting auto-reconnecting serial monitor on COM3...", flush=True)
    while True:
        try:
            s = serial.Serial('COM3', 115200, timeout=1)
            s.dtr = False
            s.rts = False
            print("Connected to COM3.", flush=True)
            while True:
                line = s.readline()
                if line:
                    line_str = line.decode('utf-8', errors='ignore')
                    if 'LVGL_TASK' not in line_str and 'FLUSH' not in line_str:
                        sys.stdout.write(line_str)
                        sys.stdout.flush()
        except serial.SerialException:
            time.sleep(1)
        except KeyboardInterrupt:
            print("Monitor stopped.", flush=True)
            break
        except Exception as e:
            time.sleep(1)

if __name__ == '__main__':
    main()
