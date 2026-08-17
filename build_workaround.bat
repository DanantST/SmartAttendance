@echo off
REM Build script for SmartAttendance on ESP32-P4
REM
REM Critical: ESP_IDF_VERSION must be set so that esp_wifi_remote's Kconfig can
REM resolve the correct version-specific sourcing:
REM   orsource "./Kconfig.idf_v$ESP_IDF_VERSION.in"
REM Without this, the entire WIFI_RMT_* Kconfig subtree is silently skipped,
REM causing undeclared-identifier compile errors for CONFIG_WIFI_RMT_TX_BUFFER_TYPE
REM and related symbols.

cd /d c:\Users\user\Documents\projects\SmartAttendance

REM --- Required build environment ---
set ESP_IDF_VERSION=5.4
set IDF_VERSION=5.4.2
set IDF_SKIP_COMPONENT_MANAGER_VERSION_CHECK=1
set PYTHONUTF8=1
set PYTHONIOENCODING=utf-8

REM Restore sdkconfig from git HEAD in case cmake clobbered it
git checkout -- sdkconfig

REM Export IDF environment (also sets PATH, IDF_PATH, etc.)
call C:\Espressif\frameworks\esp-idf-v5.4.2\export.bat esp32p4

REM Build (reconfigure picks up sdkconfig.defaults.esp32p4)
idf.py build

