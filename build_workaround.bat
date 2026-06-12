@echo off
REM Workaround script to build with component manager version checking disabled

cd /d c:\Users\user\Documents\projects\SmartAttendance

REM Set environment variable to skip version checks
set IDF_SKIP_COMPONENT_MANAGER_VERSION_CHECK=1
set PYTHONUTF8=1
set PYTHONIOENCODING=utf-8

REM Use the espressif env export
call C:\Espressif\frameworks\esp-idf-v5.4.2\export.bat esp32p4

REM Build
idf.py reconfigure
idf.py build

