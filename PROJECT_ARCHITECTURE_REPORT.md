# 📑 SmartAttendance: Project Architecture Report

## 1. Project Overview
**Goal:** The SmartAttendance project is a high-performance, edge-based biometric attendance system. It uses face recognition to identify users in real-time, logs attendance to a local SQLite database, and synchronizes records with a cloud service. It is designed for the **ESP32-P4** SoC, utilizing MIPI-DSI for the display and MIPI-CSI for the camera.

---

## 2. Core Application Logic (`/main`)

### 🛰️ System Orchestration
*   **main.c:** The central coordinator. It manages FreeRTOS tasks, the global state machine, and handles system-wide events.
*   **config.h:** Central configuration hub for system constants and feature flags.

### 👁️ Vision & Recognition
*   **detection/face_detector.cpp:** Detects faces using the `esp-dl` library.
*   **detection/face_alignment.c:** Aligns faces to a consistent 112x112 size for the recognition model.
*   **recognition/feature_extractor.cpp:** Converts aligned faces into 128-dimensional embeddings using MobileFaceNet.
*   **recognition/recognizer.cpp:** Manages user embeddings and performs similarity matching.
*   **camera/camera_driver.c:** Low-level driver for the MIPI-CSI camera interface.
*   **camera/ov5647_autofocus.c:** Controls the camera's focus motor.

### 💻 User Interface (LVGL 9)
*   **ui/ui_main.cpp:** Orchestrates the main UI flow and theme management.
*   **ui/ui_setup_wizard.cpp:** Handles initial device setup (Wi-Fi, Admin enrollment).
*   **ui/ui_enrollment.cpp:** Manages the user enrollment UI and instructions.
*   **ui/ui_user_manager.cpp:** Interface for managing enrolled users.
*   **ui/ui_reports.cpp:** Displays attendance logs and stats.
*   **display/lcd_driver.c:** Driver for the 7" MIPI-DSI IPS display.
*   **display/touch_driver.c:** Driver for the GT911 capacitive touch panel.

### 🗄️ Data & Storage
*   **database/db_manager.c:** SQLite3 wrapper for managing users and attendance logs.
*   **storage/sdcard_mount.c:** Manages SD card mounting via SDMMC.

### 🌐 Connectivity & Networking
*   **network/wifi_manager.c:** Manages Wi-Fi connections and credentials.
*   **network/cloud_sync.c:** Synchronizes logs with a remote cloud API.
*   **ble/ble_registration.c:** BLE service for mobile-app based enrollment.

---

## 3. External Components (`/components`)
*   **esp-dl:** Optimized inference engine for AI models.
*   **lvgl:** Modern graphics library for the UI.
*   **sqlite3:** Relational database for local storage.

---

## 4. Functional Contribution to Goal
The project follows a standard biometric pipeline:
1.  **Hardware Interface:** Captures images and displays the UI.
2.  **Processing:** Detects, aligns, and identifies faces at the edge.
3.  **Persistence:** Saves identification events to a local database.
4.  **Communication:** Syncs local data with the cloud for remote monitoring.
