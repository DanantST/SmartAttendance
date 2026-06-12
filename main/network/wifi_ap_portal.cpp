/**
 * @file wifi_ap_portal.cpp
 * @brief Dynamic SoftAP and Captive Portal Web Server for standalone enrollment
 */

#include "wifi_ap_portal.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "lwip/ip_addr.h"
#include "cJSON.h"
#include "ble/ble_registration.h"
#include "config.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// To update UI lists and state
#include "ui/ui_main.h"
#include "ui/ui_enrollment.h"
#include "database/db_manager.h"
#include "recognition/recognizer.h"
#include "esp_random.h"

// Declaration of functions implemented in wifi_manager.c
esp_err_t wifi_manager_connect_saved(void);

#ifdef __cplusplus
}
#endif

static const char *TAG = "WIFI_PORTAL";

static esp_netif_t *s_ap_netif = NULL;
static httpd_handle_t s_http_server = NULL;
static TaskHandle_t s_dns_task_handle = NULL;
static int s_dns_socket = -1;
static bool s_portal_running = false;

/* ─── Embedded HTML Portal Page (Dark Glassmorphism UI) ─────────────────── */
static const char* s_portal_html = R"html(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>SmartAttendance - Local Registration Portal</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;500;600;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #0b0f19;
            --card-bg: rgba(22, 30, 49, 0.7);
            --primary: #3b82f6;
            --primary-glow: rgba(59, 130, 246, 0.4);
            --success: #10b981;
            --success-glow: rgba(16, 185, 129, 0.4);
            --warning: #f59e0b;
            --danger: #ef4444;
            --text-main: #f8fafc;
            --text-muted: #94a3b8;
            --border: rgba(255, 255, 255, 0.08);
        }

        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: 'Outfit', sans-serif; background-color: var(--bg-color); color: var(--text-main); min-height: 100vh; display: flex; align-items: center; justify-content: center; overflow-x: hidden; position: relative; padding: 20px; }

        .container {
            width: 100%;
            max-width: 480px;
            background: var(--card-bg);
            backdrop-filter: blur(20px);
            -webkit-backdrop-filter: blur(20px);
            border: 1px solid var(--border);
            border-radius: 24px;
            padding: 32px;
            box-shadow: 0 20px 40px rgba(0, 0, 0, 0.3);
            z-index: 1;
        }

        .header { text-align: center; margin-bottom: 24px; }
        .logo-container { display: flex; align-items: center; justify-content: center; margin-bottom: 12px; }
        .logo-icon { width: 44px; height: 44px; fill: none; stroke: var(--primary); stroke-width: 2; filter: drop-shadow(0 0 8px var(--primary-glow)); }
        h1 { font-size: 22px; font-weight: 600; letter-spacing: -0.5px; margin-bottom: 4px; }
        .subtitle { font-size: 13px; color: var(--text-muted); }

        .tabs { display: flex; background: rgba(0, 0, 0, 0.2); border-radius: 12px; padding: 4px; margin-bottom: 24px; border: 1px solid var(--border); }
        .tab-btn { flex: 1; background: transparent; color: var(--text-muted); border: none; border-radius: 8px; padding: 10px 14px; font-size: 14px; font-weight: 500; cursor: pointer; transition: all 0.2s ease; font-family: inherit; }
        .tab-btn.active { background: var(--primary); color: white; box-shadow: 0 4px 10px rgba(59, 130, 246, 0.2); }
        .form-panel { display: none; }
        .form-panel.active { display: block; animation: fadeIn 0.3s ease forwards; }

        .form-group { margin-bottom: 16px; }
        label { display: block; font-size: 12px; font-weight: 500; color: var(--text-muted); margin-bottom: 6px; text-transform: uppercase; letter-spacing: 0.5px; }
        input { width: 100%; background: rgba(0, 0, 0, 0.2); border: 1px solid var(--border); border-radius: 10px; padding: 12px 14px; font-size: 14px; color: var(--text-main); font-family: inherit; transition: all 0.2s ease; }
        
        .btn { width: 100%; background: var(--primary); color: white; border: none; border-radius: 12px; padding: 14px; font-size: 15px; font-weight: 600; font-family: inherit; cursor: pointer; transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1); display: flex; align-items: center; justify-content: center; gap: 10px; box-shadow: 0 4px 12px rgba(59, 130, 246, 0.3); margin-top: 10px; }
        .btn:hover { transform: translateY(-1px); box-shadow: 0 6px 20px rgba(59, 130, 246, 0.4); }
        .btn:disabled { background: var(--text-muted); box-shadow: none; cursor: not-allowed; opacity: 0.6; }
        .btn-outline { background: rgba(255, 255, 255, 0.03); border: 1px solid var(--border); color: var(--text-main); box-shadow: none; }
        .btn-outline:hover { background: rgba(255, 255, 255, 0.08); }

        .error-banner { background: rgba(239, 68, 68, 0.1); border: 1px solid var(--danger); border-radius: 12px; color: #fca5a5; padding: 12px; font-size: 14px; margin-bottom: 16px; display: none; text-align: center; }

        .courses-container { border: 1px dashed var(--border); border-radius: 12px; padding: 12px; margin-bottom: 16px; background: rgba(0, 0, 0, 0.1); }
        .course-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
        .course-header span { font-size: 12px; color: var(--text-muted); font-weight: 500; }
        .course-row { display: flex; gap: 8px; margin-bottom: 8px; align-items: center; }
        .course-row input.course-code { flex: 1; }
        .course-row input.course-title { flex: 2; }
        .remove-course-btn { background: rgba(239, 68, 68, 0.1); border: 1px solid rgba(239, 68, 68, 0.3); color: #ef4444; border-radius: 8px; width: 32px; height: 32px; cursor: pointer; display: flex; align-items: center; justify-content: center; font-size: 16px; font-weight: bold; }

        .success-container { display: none; text-align: center; }
        .success-container.active { display: block; animation: fadeIn 0.4s ease forwards; }
        .spinner-outer { position: relative; width: 80px; height: 80px; margin: 20px auto 20px auto; }
        .pulse-ring { position: absolute; width: 100%; height: 100%; border-radius: 50%; background: var(--success-glow); animation: pulse 2s infinite ease-in-out; }
        .success-icon { position: absolute; top: 20px; left: 20px; width: 40px; height: 40px; fill: none; stroke: var(--success); stroke-width: 2.5; z-index: 2; }
        
        @keyframes pulse { 0% { transform: scale(0.8); opacity: 0.5; } 50% { transform: scale(1.2); opacity: 0; } 100% { transform: scale(0.8); opacity: 0.5; } }
        @keyframes fadeIn { from { opacity: 0; transform: translateY(8px); } to { opacity: 1; transform: translateY(0); } }

        .success-title { font-size: 18px; font-weight: 600; margin-bottom: 8px; }
        .success-desc { font-size: 14px; color: var(--text-muted); margin-bottom: 20px; line-height: 1.5; }
        .details-box { background: rgba(255, 255, 255, 0.02); border: 1px solid var(--border); border-radius: 12px; padding: 14px; text-align: left; margin-bottom: 20px; }
        .detail-row { display: flex; justify-content: space-between; margin-bottom: 6px; font-size: 13px; }
        .detail-label { color: var(--text-muted); }
        .detail-val { font-weight: 500; }
        .tip-banner { background: rgba(245, 158, 11, 0.1); border: 1px solid rgba(245, 158, 11, 0.3); border-radius: 12px; color: #fef08a; padding: 12px; font-size: 13px; margin-top: 15px; text-align: left; line-height: 1.4; }
        .hidden { display: none; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <div class="logo-container">
                <svg class="logo-icon" viewBox="0 0 24 24">
                    <circle cx="12" cy="12" r="10" />
                    <path d="M12 6v6l4 2" stroke-linecap="round" stroke-linejoin="round"/>
                </svg>
            </div>
            <h1>SmartAttendance</h1>
            <p class="subtitle">Enrollment & Registration Portal</p>
        </div>
        <div class="error-banner" id="errorBanner"></div>
        <div class="form-container" id="formContainer">
            <div class="tabs">
                <button class="tab-btn active" id="tabStudent" onclick="switchTab('student')">Student Enrollment</button>
                <button class="tab-btn" id="tabLecturer" onclick="switchTab('lecturer')">Lecturer Registry</button>
                <button class="tab-btn" id="tabUnenroll" onclick="switchTab('unenroll')">Unenroll Student</button>
            </div>
            <div class="form-panel active" id="panelStudent">
                <div class="form-group"><label for="studentPin">6-Digit Device PIN</label><input type="text" id="studentPin" maxlength="6" placeholder="Enter PIN from device screen" inputmode="numeric" pattern="[0-9]*"></div>
                <div class="form-group"><label for="studentName">Full Name</label><input type="text" id="studentName" placeholder="Enter full name" maxlength="64"></div>
                <div class="form-group"><label for="studentId">Matric / Student ID</label><input type="text" id="studentId" placeholder="Enter student ID" maxlength="32"></div>
                <div class="form-group"><label for="studentPhone">Phone Number (Telegram Linked)</label><input type="tel" id="studentPhone" placeholder="e.g. +2348031234567" maxlength="20"></div>
                <button class="btn" id="btnSubmitStudent" onclick="submitStudent()">Submit & Await Face Scan</button>
            </div>
            <div class="form-panel" id="panelLecturer">
                <div class="form-group"><label for="lecturerPin">6-Digit Device PIN</label><input type="text" id="lecturerPin" maxlength="6" placeholder="Enter PIN from device screen" inputmode="numeric" pattern="[0-9]*"></div>
                <div class="form-group"><label for="lecturerName">Full Name</label><input type="text" id="lecturerName" placeholder="e.g. Prof. Jane Doe" maxlength="64"></div>
                <div class="form-group"><label for="lecturerPhone">Phone Number (Telegram Linked)</label><input type="tel" id="lecturerPhone" placeholder="e.g. +2348031234567" maxlength="20"></div>
                <div class="courses-container">
                    <div class="course-header"><span>COURSES TAUGHT</span><button type="button" class="btn btn-outline" style="width: auto; padding: 4px 8px; font-size: 11px; margin-top: 0; border-radius: 6px;" onclick="addCourseRow()">+ Add Course</button></div>
                    <div id="coursesList">
                        <div class="course-row"><input type="text" class="course-code" placeholder="CS480" maxlength="10"><input type="text" class="course-title" placeholder="Computer Vision" maxlength="64"><button type="button" class="remove-course-btn" onclick="removeCourseRow(this)">×</button></div>
                    </div>
                </div>
                <button class="btn" id="btnSubmitLecturer" onclick="submitLecturer()">Register as Lecturer</button>
            </div>
            <div class="form-panel" id="panelUnenroll">
                <div class="form-group"><label for="unenrollPin">6-Digit Device PIN</label><input type="text" id="unenrollPin" maxlength="6" placeholder="Enter PIN from device screen" inputmode="numeric" pattern="[0-9]*"></div>
                <div class="form-group"><label for="unenrollStudentId">Matric / Student ID</label><input type="text" id="unenrollStudentId" placeholder="Enter student ID to unenroll" maxlength="32"></div>
                <button class="btn" style="background: var(--danger); box-shadow: 0 4px 12px rgba(239, 68, 68, 0.3);" id="btnSubmitUnenroll" onclick="submitUnenroll()">Delete Student</button>
            </div>
            <div class="tip-banner"><strong>⚠️ Connection Tip:</strong> If your phone disconnects due to "No Internet", temporarily <strong>turn off Mobile Data (LTE/5G)</strong> during registration.</div>
        </div>
        <div class="success-container" id="successContainer">
            <div class="spinner-outer"><div class="pulse-ring"></div><svg class="success-icon" viewBox="0 0 24 24"><polyline points="20 6 9 17 4 12" stroke-linecap="round" stroke-linejoin="round"/></svg></div>
            <div class="success-title" id="successTitle">Successfully Registered!</div>
            <p class="success-desc" id="successDesc">Your details have been saved successfully.</p>
            <div class="details-box" id="successDetails"></div>
            <button class="btn btn-outline" id="btnBack" onclick="resetForm()">Register Another</button>
        </div>
    </div>
    <script>
        let currentTab = 'student';
        function switchTab(tab) {
            currentTab = tab; clearError();
            document.getElementById('tabStudent').classList.toggle('active', tab === 'student');
            document.getElementById('tabLecturer').classList.toggle('active', tab === 'lecturer');
            document.getElementById('tabUnenroll').classList.toggle('active', tab === 'unenroll');
            document.getElementById('panelStudent').classList.toggle('active', tab === 'student');
            document.getElementById('panelLecturer').classList.toggle('active', tab === 'lecturer');
            document.getElementById('panelUnenroll').classList.toggle('active', tab === 'unenroll');
        }
        function addCourseRow() {
            const list = document.getElementById('coursesList');
            const row = document.createElement('div'); row.className = 'course-row';
            row.innerHTML = `<input type="text" class="course-code" placeholder="ECE320" maxlength="10"><input type="text" class="course-title" placeholder="Embedded Systems" maxlength="64"><button type="button" class="remove-course-btn" onclick="removeCourseRow(this)">×</button>`;
            list.appendChild(row);
        }
        function removeCourseRow(btn) { const list = document.getElementById('coursesList'); if (list.children.length > 1) btn.parentElement.remove(); else alert("At least one course must be specified."); }
        function showError(msg) { const banner = document.getElementById('errorBanner'); banner.textContent = msg; banner.style.display = 'block'; window.scrollTo(0, 0); }
        function clearError() { document.getElementById('errorBanner').style.display = 'none'; }
        async function submitStudent() {
            clearError(); const pin = document.getElementById('studentPin').value.trim(); const name = document.getElementById('studentName').value.trim(); const student_id = document.getElementById('studentId').value.trim(); const phone = document.getElementById('studentPhone').value.trim();
            if (!pin || !name || !student_id || !phone) { showError("All fields are required."); return; }
            if (pin.length !== 6) { showError("The PIN must be exactly 6 digits."); return; }
            const btn = document.getElementById('btnSubmitStudent'); btn.disabled = true; btn.textContent = "Submitting...";
            try {
                const response = await fetch('/submit_student', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ pin, name, student_id, phone_number: phone }) });
                if (response.ok) { showSuccess('Student Submitted!', 'Your details are queued. Please stand in front of the camera when prompted.', `<div class="detail-row"><span class="detail-label">Name:</span><span class="detail-val">${name}</span></div><div class="detail-row"><span class="detail-label">ID:</span><span class="detail-val">${student_id}</span></div>`); }
                else showError(await response.text() || "Submission failed.");
            } catch (err) { showError("Network failure."); } finally { btn.disabled = false; btn.textContent = "Submit & Await Face Scan"; }
        }
        async function submitLecturer() {
            clearError(); const pin = document.getElementById('lecturerPin').value.trim(); const name = document.getElementById('lecturerName').value.trim(); const phone = document.getElementById('lecturerPhone').value.trim();
            if (!pin || !name || !phone) { showError("All fields are required."); return; }
            const courses = Array.from(document.querySelectorAll('#coursesList .course-row')).map(row => ({ code: row.querySelector('.course-code').value.trim(), title: row.querySelector('.course-title').value.trim() }));
            if (courses.some(c => !c.code || !c.title)) { showError("Please fill all course fields."); return; }
            const btn = document.getElementById('btnSubmitLecturer'); btn.disabled = true; btn.textContent = "Submitting...";
            try {
                const response = await fetch('/submit_lecturer', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ pin, name, phone_number: phone, courses }) });
                if (response.ok) { showSuccess('Lecturer Registered!', 'Success!', `<div class="detail-row"><span class="detail-label">Name:</span><span class="detail-val">${name}</span></div>`); }
                else showError(await response.text() || "Submission failed.");
            } catch (err) { showError("Network failure."); } finally { btn.disabled = false; btn.textContent = "Register as Lecturer"; }
        }
        async function submitUnenroll() {
            clearError(); const pin = document.getElementById('unenrollPin').value.trim(); const student_id = document.getElementById('unenrollStudentId').value.trim();
            if (!pin || !student_id) { showError("All fields are required."); return; }
            if (pin.length !== 6) { showError("The PIN must be exactly 6 digits."); return; }
            if (!confirm("Are you sure you want to delete this student and all their logs?")) return;
            const btn = document.getElementById('btnSubmitUnenroll'); btn.disabled = true; btn.textContent = "Processing...";
            try {
                const response = await fetch('/unenroll_student', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ pin, student_id }) });
                if (response.ok) { showSuccess('Student Unenrolled!', 'The student details and attendance logs have been removed.', `<div class="detail-row"><span class="detail-label">ID:</span><span class="detail-val">${student_id}</span></div>`); }
                else showError(await response.text() || "Unenrollment failed.");
            } catch (err) { showError("Network failure."); } finally { btn.disabled = false; btn.textContent = "Delete Student"; }
        }
        function showSuccess(title, desc, detailsHtml) { document.getElementById('successTitle').textContent = title; document.getElementById('successDesc').textContent = desc; document.getElementById('successDetails').innerHTML = detailsHtml; document.getElementById('formContainer').classList.add('hidden'); document.getElementById('successContainer').classList.add('active'); }
        function resetForm() { document.getElementById('successContainer').classList.remove('active'); document.getElementById('formContainer').classList.remove('hidden'); clearError(); }
    </script>
</body>
</html>)html";

/* ─── HTTP Server handlers ──────────────────────────────────────────────── */

static esp_err_t get_portal_handler(httpd_req_t *req) {
    char host_hdr[64] = {0};
    httpd_req_get_hdr_value_str(req, "Host", host_hdr, sizeof(host_hdr));
    
    // Serve portal only if accessing the root or index page, or if already using the gateway IP
    if (strcmp(req->uri, "/") == 0 || 
        strcmp(req->uri, "/index.html") == 0 || 
        strstr(host_hdr, "192.168.4.1") != NULL) {
        
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, s_portal_html, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    // For background connectivity probes (e.g. apple.com, gstatic.com), return an explicit 302 Redirect
    ESP_LOGI(TAG, "Redirecting probe '%s' (Host: %s) to captive portal", req->uri, host_hdr);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/index.html");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t post_submit_student_handler(httpd_req_t *req) {
    char *buf = (char *)malloc(512);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    int remaining = req->content_len;
    
    if (remaining >= 512) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_FAIL;
    }
    
    int bytes_read = 0;
    while (remaining > 0) {
        int ret = httpd_req_recv(req, buf + bytes_read, remaining);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(buf);
            return ESP_FAIL;
        }
        bytes_read += ret;
        remaining -= ret;
    }
    buf[bytes_read] = '\0';
    
    // Parse fields from JSON payload
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }
    
    cJSON *pin_json = cJSON_GetObjectItem(root, "pin");
    cJSON *name_json = cJSON_GetObjectItem(root, "name");
    cJSON *id_json = cJSON_GetObjectItem(root, "student_id");
    cJSON *phone_json = cJSON_GetObjectItem(root, "phone_number");
    
    if (!pin_json || !name_json || !id_json || !phone_json) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing fields");
        return ESP_OK;
    }
    
    const char *pin = pin_json->valuestring;
    const char *name = name_json->valuestring;
    const char *id = id_json->valuestring;
    const char *phone = phone_json->valuestring;
    
    // Validate PIN
    const char *active_pin = ble_registration_get_pin();
    if (!active_pin || strcmp(pin, active_pin) != 0) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Invalid enrollment PIN. Check device screen.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    // Validate that student_id is not already registered in database
    if (db_student_id_exists(id)) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_send(req, "Matric ID already registered in database.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    // Queue student
    esp_err_t err = ble_registration_add_pending_student(name, id, "student", phone);
    cJSON_Delete(root);

    
    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "SUCCESS");
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Device queue is currently full. Try again later.", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

static esp_err_t post_submit_lecturer_handler(httpd_req_t *req) {
    char *buf = (char *)malloc(1024);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    int remaining = req->content_len;
    
    if (remaining >= 1024) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_FAIL;
    }
    
    int bytes_read = 0;
    while (remaining > 0) {
        int ret = httpd_req_recv(req, buf + bytes_read, remaining);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(buf);
            return ESP_FAIL;
        }
        bytes_read += ret;
        remaining -= ret;
    }
    buf[bytes_read] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }
    
    cJSON *pin_json = cJSON_GetObjectItem(root, "pin");
    cJSON *name_json = cJSON_GetObjectItem(root, "name");
    cJSON *phone_json = cJSON_GetObjectItem(root, "phone_number");
    cJSON *courses_json = cJSON_GetObjectItem(root, "courses");
    
    if (!pin_json || !name_json || !phone_json || !courses_json || !cJSON_IsArray(courses_json)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid fields");
        return ESP_OK;
    }
    
    const char *pin = pin_json->valuestring;
    const char *name = name_json->valuestring;
    const char *phone = phone_json->valuestring;
    
    // Validate PIN
    const char *active_pin = ble_registration_get_pin();
    if (!active_pin || strcmp(pin, active_pin) != 0) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Invalid enrollment PIN. Check device screen.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    // Generate a UUID for lecturer
    char uuid_str[37];
    uint8_t rand_bytes[16];
    esp_fill_random(rand_bytes, 16);
    snprintf(uuid_str, sizeof(uuid_str), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             rand_bytes[0], rand_bytes[1], rand_bytes[2], rand_bytes[3],
             rand_bytes[4], rand_bytes[5], rand_bytes[6], rand_bytes[7],
             rand_bytes[8], rand_bytes[9], rand_bytes[10], rand_bytes[11],
             rand_bytes[12], rand_bytes[13], rand_bytes[14], rand_bytes[15]);
             
    user_t *lecturer = (user_t *)calloc(1, sizeof(user_t));
    if (!lecturer) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    strncpy(lecturer->uuid, uuid_str, sizeof(lecturer->uuid) - 1);
    strncpy(lecturer->name, name, sizeof(lecturer->name) - 1);
    strncpy(lecturer->phone_number, phone, sizeof(lecturer->phone_number) - 1);
    strcpy(lecturer->role, "lecturer");
    lecturer->created_at = time(NULL);
    lecturer->updated_at = time(NULL);
    
    esp_err_t err = db_insert_lecturer(lecturer);
    if (err != ESP_OK) {
        free(lecturer);
        cJSON_Delete(root);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Failed to insert lecturer into database.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    int num_courses = cJSON_GetArraySize(courses_json);
    for (int i = 0; i < num_courses; i++) {
        cJSON *item = cJSON_GetArrayItem(courses_json, i);
        cJSON *code_json = cJSON_GetObjectItem(item, "code");
        cJSON *title_json = cJSON_GetObjectItem(item, "title");
        if (code_json && title_json) {
            int course_id = 0;
            if (db_insert_or_get_course(code_json->valuestring, title_json->valuestring, &course_id) == ESP_OK) {
                db_link_lecturer_course(lecturer->id, course_id);
            }
        }
    }
    
    free(lecturer);
    cJSON_Delete(root);
    httpd_resp_sendstr(req, "SUCCESS");
    return ESP_OK;
}

static esp_err_t post_unenroll_student_handler(httpd_req_t *req) {
    char *buf = (char *)malloc(512);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    int remaining = req->content_len;
    
    if (remaining >= 512) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_FAIL;
    }
    
    int bytes_read = 0;
    while (remaining > 0) {
        int ret = httpd_req_recv(req, buf + bytes_read, remaining);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(buf);
            return ESP_FAIL;
        }
        bytes_read += ret;
        remaining -= ret;
    }
    buf[bytes_read] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }
    
    cJSON *pin_json = cJSON_GetObjectItem(root, "pin");
    cJSON *id_json = cJSON_GetObjectItem(root, "student_id");
    
    if (!pin_json || !id_json) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing fields");
        return ESP_OK;
    }
    
    const char *pin = pin_json->valuestring;
    const char *id = id_json->valuestring;
    
    // Validate PIN
    const char *active_pin = ble_registration_get_pin();
    if (!active_pin || strcmp(pin, active_pin) != 0) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Invalid enrollment PIN. Check device screen.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    cJSON_Delete(root);
    
    esp_err_t err = db_delete_user_by_student_id(id);
    if (err == ESP_OK) {
        // Refresh face recognizer cache
        recognizer_load_cache();
        httpd_resp_sendstr(req, "SUCCESS");
    } else if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "Student ID not found in database.", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Failed to delete student from database.", HTTPD_RESP_USE_STRLEN);
    }
    
    return ESP_OK;
}

static const httpd_uri_t s_submit_uri = {
    .uri       = "/submit",
    .method    = HTTP_POST,
    .handler   = post_submit_student_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t s_submit_student_uri = {
    .uri       = "/submit_student",
    .method    = HTTP_POST,
    .handler   = post_submit_student_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t s_submit_lecturer_uri = {
    .uri       = "/submit_lecturer",
    .method    = HTTP_POST,
    .handler   = post_submit_lecturer_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t s_unenroll_student_uri = {
    .uri       = "/unenroll_student",
    .method    = HTTP_POST,
    .handler   = post_unenroll_student_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t s_wildcard_uri = {
    .uri       = "*",
    .method    = HTTP_GET,
    .handler   = get_portal_handler,
    .user_ctx  = NULL
};

static esp_err_t start_http_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 10240; // Increase stack size to prevent SQLite stack overflow crashes
    config.max_open_sockets = 4;
    config.ctrl_port = 32768; // safe control port
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    
    ESP_LOGI(TAG, "Starting HTTP Server on port %d", config.server_port);
    esp_err_t err = httpd_start(&s_http_server, &config);
    if (err == ESP_OK) {
        httpd_register_uri_handler(s_http_server, &s_submit_uri);
        httpd_register_uri_handler(s_http_server, &s_submit_student_uri);
        httpd_register_uri_handler(s_http_server, &s_submit_lecturer_uri);
        httpd_register_uri_handler(s_http_server, &s_unenroll_student_uri);
        httpd_register_uri_handler(s_http_server, &s_wildcard_uri);
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server: %d", err);
    }
    return err;
}

static void stop_http_server(void) {
    if (s_http_server) {
        httpd_stop(s_http_server);
        s_http_server = NULL;
        ESP_LOGI(TAG, "HTTP Server stopped");
    }
}

/* ─── DNS Captive Portal Redirector (UDP Port 53) ───────────────────────── */

static void dns_redirect_task(void *pvParameters) {
    (void)pvParameters;
    uint8_t rx_buffer[512];
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(53);
    
    s_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_dns_socket < 0) {
        ESP_LOGE(TAG, "Unable to create DNS socket: errno %d", errno);
        s_dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    int err = bind(s_dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Unable to bind DNS socket: errno %d", errno);
        close(s_dns_socket);
        s_dns_socket = -1;
        s_dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "DNS Server listening on port 53");
    
    while (1) {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(s_dns_socket, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&source_addr, &socklen);
        if (len < 0) {
            // Error or socket closed by stop call
            break;
        }
        
        if (len > 12) {
            // DNS Header flag manipulations to return a response
            rx_buffer[2] |= 0x80; // QR = 1 (Response)
            rx_buffer[3] |= 0x80; // RA = 1 (Recursion Available)
            
            // Set Answer RRs Count to 1
            rx_buffer[6] = 0;
            rx_buffer[7] = 1;
            
            // Clear Authority and Additional records to avoid client parsing issues
            rx_buffer[8] = 0;
            rx_buffer[9] = 0;
            rx_buffer[10] = 0;
            rx_buffer[11] = 0;
            
            int response_len = len;
            
            // Construct DNS A Record reply pointing to 192.168.4.1 (SoftAP IP)
            // Pointer to query name (offset 12)
            rx_buffer[response_len++] = 0xC0;
            rx_buffer[response_len++] = 0x0C;
            
            // Type: A (0x0001)
            rx_buffer[response_len++] = 0x00;
            rx_buffer[response_len++] = 0x01;
            
            // Class: IN (0x0001)
            rx_buffer[response_len++] = 0x00;
            rx_buffer[response_len++] = 0x01;
            
            // TTL: 10 seconds (0x0000000a)
            rx_buffer[response_len++] = 0x00;
            rx_buffer[response_len++] = 0x00;
            rx_buffer[response_len++] = 0x00;
            rx_buffer[response_len++] = 0x0A;
            
            // Data length: 4 bytes (IPv4)
            rx_buffer[response_len++] = 0x00;
            rx_buffer[response_len++] = 0x04;
            
            // IP address: 192.168.4.1
            rx_buffer[response_len++] = 192;
            rx_buffer[response_len++] = 168;
            rx_buffer[response_len++] = 4;
            rx_buffer[response_len++] = 1;
            
            sendto(s_dns_socket, rx_buffer, response_len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
        }
    }
    
    if (s_dns_socket >= 0) {
        close(s_dns_socket);
        s_dns_socket = -1;
    }
    s_dns_task_handle = NULL;
    ESP_LOGI(TAG, "DNS Server stopped");
    vTaskDelete(NULL);
}

static void start_dns_server(void) {
    xTaskCreate(dns_redirect_task, "dns_portal", 4096, NULL, 5, &s_dns_task_handle);
}

static void stop_dns_server(void) {
    if (s_dns_socket >= 0) {
        // Closing the socket immediately forces recvfrom to fail, unblocking the task
        close(s_dns_socket);
        s_dns_socket = -1;
    }
    // Task deletes itself upon loop termination; no need to delete from here
}

/* ─── Public API ────────────────────────────────────────────────────────── */

esp_err_t wifi_ap_portal_start(void) {
    if (s_portal_running) return ESP_OK;
    ESP_LOGW(TAG, "=============================================");
    ESP_LOGW(TAG, " STARTING STANDALONE WI-FI CAPTIVE PORTAL");
    ESP_LOGW(TAG, "=============================================");

    // 1. Temporarily stop any STA active scans or connect tasks
    esp_wifi_disconnect();
    esp_wifi_stop();

    // 2. Initialize default AP netif if not already done
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (!s_ap_netif) {
            s_ap_netif = esp_netif_create_default_wifi_ap();
        }
        if (!s_ap_netif) {
            ESP_LOGE(TAG, "Failed to create default Wi-Fi AP interface");
            return ESP_ERR_NO_MEM;
        }
    }

    // 3. Configure Wi-Fi to AP Mode
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
         ESP_LOGE(TAG, "esp_wifi_set_mode failed: %d", ret);
         return ret;
    }

    wifi_config_t ap_config;
    memset(&ap_config, 0, sizeof(ap_config));
    strcpy((char*)ap_config.ap.ssid, "Attendance_Setup");
    ap_config.ap.ssid_len = strlen("Attendance_Setup");
    ap_config.ap.channel = 1;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 8;

    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %d", ret);
        return ret;
    }

    // 4. Start Wi-Fi AP
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %d", ret);
        return ret;
    }
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(50);
    ESP_LOGI(TAG, "SoftAP started. SSID: Attendance_Setup (IP: 192.168.4.1)");

    // Ensure the DHCP server is running on the AP netif with correct IP settings
    esp_netif_dhcps_stop(s_ap_netif);
    
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));
    ip_info.ip.addr = ipaddr_addr("192.168.4.1");
    ip_info.gw.addr = ipaddr_addr("192.168.4.1");
    ip_info.netmask.addr = ipaddr_addr("255.255.255.0");
    
    esp_err_t ip_err = esp_netif_set_ip_info(s_ap_netif, &ip_info);
    if (ip_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set static IP info on AP netif: %d", ip_err);
    }
    
    esp_err_t dhcp_err = esp_netif_dhcps_start(s_ap_netif);
    if (dhcp_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start DHCP server: %d", dhcp_err);
    } else {
        ESP_LOGI(TAG, "DHCP Server started successfully.");
    }

    // 5. Start Servers
    start_dns_server();
    start_http_server();

    s_portal_running = true;
    return ESP_OK;
}

void wifi_ap_portal_stop(void) {
    if (!s_portal_running) return;
    ESP_LOGI(TAG, "Stopping Standalone Wi-Fi Captive Portal...");

    // 1. Stop web servers
    stop_dns_server();
    stop_http_server();

    // 2. Shut down Wi-Fi AP
    esp_wifi_stop();

    // 3. Re-enable Wi-Fi STA mode
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(50);

    s_portal_running = false;
    ESP_LOGI(TAG, "SoftAP stopped. Wi-Fi returned to STA mode.");

    // 4. Re-establish connection to saved station if credentials exist
    wifi_manager_connect_saved();
}
