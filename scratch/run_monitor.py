import serial
import time
import sys

def main():
    print("Opening serial port COM5...")
    try:
        s = serial.Serial('COM5', 115200, timeout=0.1)
    except Exception as e:
        print(f"Error opening port: {e}")
        return

    print("Successfully opened COM5. Resetting board...")
    s.setDTR(False)
    s.setRTS(True)
    time.sleep(0.1)
    s.setRTS(False)
    time.sleep(0.1)
    s.setDTR(True)
    
    s.reset_input_buffer()
    
    print("--- Serial Monitor Started (120 Seconds) ---")
    end_time = time.time() + 120.0
    while time.time() < end_time:
        try:
            line = s.readline()
            if line:
                sys.stdout.write(line.decode('utf-8', errors='replace'))
                sys.stdout.flush()
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"\nRead error: {e}")
            break
            
    print("\n--- Serial Monitor Stopped ---")
    s.close()

if __name__ == '__main__':
    main()
