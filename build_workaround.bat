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

REM Export IDF environment (also sets PATH, IDF_PATH, etc.)
call C:\Espressif\frameworks\esp-idf-v5.4.2\export.bat esp32p4

REM Full clean to purge stale CMake/Python-env cache (safe to skip if env hasn't changed)
idf.py fullclean

REM Restore sdkconfig from git HEAD AFTER fullclean (fullclean can remove build/config)
git checkout -- sdkconfig

REM Build (reconfigure picks up sdkconfig.defaults.esp32p4)
idf.py build

