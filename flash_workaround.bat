@echo off
REM Workaround script to flash + monitor with correct env (mirrors build_workaround.bat)

cd /d c:\Users\user\Documents\projects\SmartAttendance

set IDF_SKIP_COMPONENT_MANAGER_VERSION_CHECK=1
set PYTHONUTF8=1
set PYTHONIOENCODING=utf-8

call C:\Espressif\frameworks\esp-idf-v5.4.2\export.bat esp32p4

idf.py -p COM5 flash monitor
