import serial
import time
import sys

def main():
    port = 'COM3'
    baud = 115200
    duration = 600  # 10 minutes
    log_file = "scratch/monitor_live.log"
    print(f"Opening serial port {port} @ {baud} (logging to {log_file})...")
    sys.stdout.flush()
    end_time = time.time() + duration
    
    with open(log_file, "a", encoding="utf-8", errors="replace") as f_out:
        f_out.write(f"\n--- Monitor Session Started at {time.strftime('%Y-%m-%d %H:%M:%S')} ---\n")
        f_out.flush()
        s = None
        while time.time() < end_time:
            if s is None:
                try:
                    s = serial.Serial(port, baud, timeout=0.2)
                    print(f"\n--- Serial Monitor Connected on {port} ---")
                    sys.stdout.flush()
                    s.reset_input_buffer()
                except Exception:
                    time.sleep(0.5)
                    continue
            
            try:
                line = s.readline()
                if line:
                    text = line.decode('utf-8', errors='replace')
                    sys.stdout.write(text)
                    sys.stdout.flush()
                    f_out.write(text)
                    f_out.flush()
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"\nRead error / Disconnect: {e}")
                sys.stdout.flush()
                try:
                    s.close()
                except Exception:
                    pass
                s = None
                time.sleep(0.5)

        if s is not None:
            s.close()
        print("\n--- Serial Monitor Stopped ---")
        f_out.write(f"\n--- Monitor Session Stopped at {time.strftime('%Y-%m-%d %H:%M:%S')} ---\n")
        f_out.flush()

if __name__ == '__main__':
    main()
