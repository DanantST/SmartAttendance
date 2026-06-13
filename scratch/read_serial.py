import serial
import sys
import time

port = 'COM4'
baud = 115200

print(f"Connecting to {port} at {baud}...")
try:
    ser = serial.Serial(port, baud, timeout=1)
    print("Connected successfully. Streaming logs:")
    while True:
        line = ser.readline()
        if line:
            sys.stdout.write(line.decode('utf-8', errors='replace'))
            sys.stdout.flush()
except KeyboardInterrupt:
    print("\nExiting.")
except Exception as e:
    print(f"Error: {e}")
