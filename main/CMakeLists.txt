cmake_minimum_required(VERSION 3.16)

# Conditionally include OV5647-specific autofocus source only when that sensor is selected
if(CONFIG_CAMERA_SENSOR_OV5647)
    set(CAMERA_EXTRA_SRCS camera/ov5647_autofocus.c)
else()
    set(CAMERA_EXTRA_SRCS "main.c" "")
endif()

idf_component_register(SRCS
    main.c
    boards/elecrow_p4_board.c
    camera/camera_driver.c
    ${CAMERA_EXTRA_SRCS}
    detection/face_detector.cpp
    detection/face_alignment.c
    recognition/feature_extractor.cpp
    recognition/recognizer.cpp
    database/db_manager.c
    storage/sdcard_mount.c
    display/lcd_driver.c
    display/touch_driver.c
    display/backlight.c
    ui/ui_main.cpp
    ui/ui_attendance.cpp
    ui/ui_file_manager.cpp
    ui/ui_components.cpp
    ui/ui_enrollment.cpp
    ui/ui_settings.cpp
    ui/ui_reports.cpp
    ui/ui_setup_wizard.cpp
    ui/ui_admin_setup.cpp
    ui/ui_user_manager.cpp
    ui/ui_theme.cpp
    ble/ble_registration.c
    network/wifi_manager.c
    network/wifi_ap_portal.cpp
    network/cloud_sync.c
    network/sntp_sync.c
    power/battery_monitor.c
    utils/queue_manager.c
    network/ota_manager.c
    INCLUDE_DIRS
    .
    boards
    camera
    detection
    recognition
    database
    storage
    display
    ui
    ble
    network
    power
    utils
    ota
    REQUIRES
    freertos
    driver
    esp_driver_i2s
    esp_driver_i2c
    esp_driver_ledc
    esp_lcd
    esp_wifi
    esp_event
    nvs_flash
    sdmmc
    fatfs
    lvgl
    esp-dl
    sqlite3
    esp_http_client
    esp_http_server
    mbedtls
    log
    esp_adc
    human_face_detect
    human_face_recognition
    esp_driver_cam
    esp_driver_isp
    esp_cam_sensor
    esp_hw_support
    esp_https_ota
    app_update
)

# SPIFFS partition image removed 2026-06-11 (audio subsystem stripped).
# The 'storage' SPIFFS partition is no longer used.

# Force inclusion of MIPI-CSI camera sensor drivers for auto-detection.
# The CrowPanel camera is at I2C addr 0x36 - candidates: ov5647, ov2710, sc202cs
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u ov5647_detect")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u ov2710_detect")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u sc202cs_detect")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u sc2336_detect")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u gc2145_detect")
