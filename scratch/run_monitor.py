import serial
import time
import sys

def main():
    print("Opening serial port COM4 with auto-reconnect...")
    duration = 300  # 5 minutes
    end_time = time.time() + duration
    
    s = None
    while time.time() < end_time:
        if s is None:
            try:
                s = serial.Serial('COM4', 115200, timeout=0.1)
                print("\n--- Serial Monitor Connected ---")
                s.reset_input_buffer()
            except Exception as e:
                # print(".", end="")
                sys.stdout.write(".")
                sys.stdout.flush()
                time.sleep(0.5)
                continue
        
        try:
            line = s.readline()
            if line:
                text = line.decode('utf-8', errors='replace')
                sys.stdout.write(text)
                sys.stdout.flush()
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"\nRead error / Disconnect: {e}")
            try:
                s.close()
            except:
                pass
            s = None
            time.sleep(0.5)

    if s is not None:
        s.close()
    print("\n--- Serial Monitor Stopped ---")

if __name__ == '__main__':
    main()
