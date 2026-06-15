detailed_log = """### [2026-06-13 17:30] — Camera Auto-Exposure, Wi-Fi Connection Wait & Telegram ID Sync

- **Category:** Camera / Sync / Database / Backend
- **Altered Files:**
  - `main/camera/camera_driver.c` (modified)
  - `main/database/db_manager.c` / `db_manager.h` (modified)
  - `main/network/cloud_sync.c` (modified)
  - `main/main.c` (modified)
  - `telegram_bot/bot.py` (modified)
- **Status:** \u2705 Completed

---

#### Detailed Summary of Issues & Technical Resolutions

##### 1. Camera Auto-Exposure (AE) Control Feedback Loop
- **The Issue:**
  The camera feed was either too dark in low light or too bright in natural sunlight, making face recognition similarity scores drop and leading to unreliable recognition in non-controlled lighting.
- **The Resolution:**
  Implemented a software AE feedback loop using the ESP32-P4's hardware ISP AE environment statistics. We register an AE environment detector statistics callback `ae_stats_cb` in IRAM which calculates the average luminance across the 5x5 sample grid. Every 10 frames (~330ms), a proportional feedback loop compares this average luminance to a target of `120` (within a deadband of `15`). If too dark, it increments exposure (`ESP_CAM_SENSOR_EXPOSURE_VAL`) up to 1500, then gain (`ESP_CAM_SENSOR_GAIN`) up to 6000. If too bright, it decrements gain, then exposure.
- **Code Implementation Details (`main/camera/camera_driver.c`):**
  ```c
  static IRAM_ATTR bool ae_stats_cb(isp_ae_ctlr_t ae_ctlr,
                                    const esp_isp_ae_env_detector_evt_data_t *edata,
                                    void *user_data)
  {
      uint32_t sum = 0;
      for (int i = 0; i < 5; i++) {
          for (int j = 0; j < 5; j++) {
              sum += edata->ae_result.luminance[i][j];
          }
      }
      s_latest_luminance = sum / 25;
      s_ae_stats_ready = true;
      return false;
  }
  ```

##### 2. Wi-Fi Connection Wait in Sync Cycle
- **The Issue:**
  The `network_sync_task` in `main.c` was immediately running time sync and cloud sync right after calling `wifi_manager_connect_saved()`. Since Wi-Fi connection is asynchronous, the requests would run before an IP address was obtained, causing SNTP and cloud sync requests to fail with immediate network timeouts.
- **The Resolution:**
  Added a connection status wait loop to block `network_sync_task` until the status transitions to `WIFI_STATUS_CONNECTED` (or a timeout of 30 seconds is reached) before attempting SNTP sync and cloud sync.
- **Code Implementation Details (`main/main.c`):**
  ```c
  if (wifi_manager_connect_saved() == ESP_OK) {
      int wait_limit = 600; // 600 * 50ms = 30 seconds
      while (wifi_manager_get_status() != WIFI_STATUS_CONNECTED &&
             wifi_manager_get_status() != WIFI_STATUS_CONNECTION_FAILED &&
             wait_limit > 0) {
          vTaskDelay(pdMS_TO_TICKS(50));
          wait_limit--;
      }
      if (wifi_manager_get_status() == WIFI_STATUS_CONNECTED) {
          sntp_sync_init();
          // ... proceed with cloud sync
      }
  }
  ```

##### 3. Telegram ID Mapping & Sync-Down
- **The Issue:**
  Users who registered their Telegram account with the bot had their `telegram_id` set in the cloud database, but this change never synced back to the device SQLite database, leaving the device UI permanently displaying "Telegram ID: Not Linked".
- **The Resolution:**
  Extended the backend `sync_users` endpoint in `bot.py` to select all users with non-empty `telegram_id` and return them as a list of mappings (`[{"uuid": ..., "telegram_id": ...}]`). Added `db_update_user_telegram_id()` to the device SQLite manager to update `telegram_id` for a user UUID. Modified `sync_users()` in `cloud_sync.c` to parse the returned mappings list and call `db_update_user_telegram_id()` for each mapping.
- **Code Implementation Details (`main/database/db_manager.c`):**
  ```c
  esp_err_t db_update_user_telegram_id(const char* uuid, const char* telegram_id) {
      if (!s_initialized) return ESP_ERR_INVALID_STATE;
      if (!uuid || !telegram_id) return ESP_ERR_INVALID_ARG;
      DB_LOCK();
      sqlite3_stmt *stmt;
      const char *sql = "UPDATE users SET telegram_id = ?, updated_at = strftime('%s','now') WHERE uuid = ?";
      int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
      if (rc != SQLITE_OK) {
          ESP_LOGE(TAG, "Prepare update telegram_id failed: %s", sqlite3_errmsg(s_db));
          DB_UNLOCK();
          return ESP_FAIL;
      }
      sqlite3_bind_text(stmt, 1, telegram_id, -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 2, uuid, -1, SQLITE_STATIC);
      rc = sqlite3_step(stmt);
      sqlite3_finalize(stmt);
      DB_UNLOCK();
      if (rc != SQLITE_DONE) {
          ESP_LOGE(TAG, "Update telegram_id failed: %s", sqlite3_errmsg(s_db));
          return ESP_FAIL;
      }
      return ESP_OK;
  }
  ```
"""

with open("c:/Users/user/Documents/projects/SmartAttendance/activity_log.md", "r", encoding="utf-8") as f:
    lines = f.readlines()

new_lines = []
for line in lines:
    if "### [2026-06-13 17:30]" in line:
        break
    new_lines.append(line)

# Clean trailing whitespace/newlines from the end before appending
while new_lines and new_lines[-1].strip() == "":
    new_lines.pop()
if new_lines and not new_lines[-1].endswith("\n"):
    new_lines[-1] += "\n"

new_content = "".join(new_lines) + "\n\n" + detailed_log

with open("c:/Users/user/Documents/projects/SmartAttendance/activity_log.md", "w", encoding="utf-8") as f:
    f.write(new_content)

print("Updated activity_log.md with detailed logs!")
