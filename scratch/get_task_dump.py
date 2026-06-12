import serial
import time
import sys

port = 'COM5'
baud = 115200

print(f"Connecting to {port} at {baud}...")
try:
    ser = serial.Serial(port, baud, timeout=2)
    print("Sending command: task-dump")
    ser.write(b"\r") # send carriage return to wake up prompt
    time.sleep(0.1)
    ser.write(b"task-dump\r\n")
    
    # Read response for 5 seconds
    start_time = time.time()
    while time.time() - start_time < 5:
        line = ser.readline()
        if line:
            sys.stdout.write(line.decode('utf-8', errors='replace'))
            sys.stdout.flush()
        else:
            time.sleep(0.05)
            
    # Also get cpu-dump for extra detail
    print("\nSending command: cpu-dump")
    ser.write(b"cpu-dump\r\n")
    start_time = time.time()
    while time.time() - start_time < 3:
        line = ser.readline()
        if line:
            sys.stdout.write(line.decode('utf-8', errors='replace'))
            sys.stdout.flush()
        else:
            time.sleep(0.05)

except Exception as e:
    print(f"Error: {e}")
