#!/usr/bin/env python
# Workaround script to skip component manager version checking
import os
import sys

# Set environment variable to skip version checks
os.environ['IDF_SKIP_COMPONENT_MANAGER_VERSION_CHECK'] = '1'

# Change to project directory
os.chdir(r'c:\Users\user\Documents\projects\SmartAttendance')

# Import and run the ESP-IDF build
import subprocess
result = subprocess.run([
    sys.executable, 
    r'C:\esp\v6.0\esp-idf\tools\idf.py',
    'build'
], env=os.environ)

sys.exit(result.returncode)
