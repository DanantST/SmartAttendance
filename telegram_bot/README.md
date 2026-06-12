# SmartAttendance Telegram Bot & Sync API

This directory contains the Python Telegram bot and REST API services for the **Edge-AI Face Recognition Smart Attendance Device**. It acts as the cloud backend that synchronizes data with the offline hardware device, manages lecture scheduling for lecturers, and sends notifications.

---

## Architecture Overview

1. **Telegram Bot**: Built using `python-telegram-bot` (v21.3 async). It guides lecturers through a conversational form to schedule classes and matches their registration via phone-number sharing.
2. **REST API**: Built using `FastAPI` and `Uvicorn`. It runs concurrently in the same process, exposing:
   - `POST /api/sync_users`: The ESP32 device pushes registered students, lecturers, and admins to the cloud.
   - `GET /api/get_schedules`: The ESP32 device pulls new schedules created on the Telegram bot.
3. **Database**: A lightweight SQLite database (`bot_data.db`) storing user state and created schedules.

---

## Local Setup

### 1. Prerequisites
- Python 3.9 or higher installed.

### 2. Installation
Clone/navigate to the `telegram_bot` directory and set up a virtual environment:

```bash
# Create virtual environment
python -m venv .venv

# Activate virtual environment
# On Windows:
.venv\Scripts\activate
# On macOS/Linux:
source .venv/bin/activate

# Install dependencies
pip install -r requirements.txt
```

### 3. Run Locally
Execute the runner script:
```bash
python bot.py
```
This launches the Telegram bot polling and the FastAPI server on `http://0.0.0.0:8000`.

---

## API Endpoints (For ESP32 Integration)

### 1. Synchronize Users
* **Endpoint**: `POST /api/sync_users`
* **Content-Type**: `application/json`
* **Request Body**:
  ```json
  [
    {
      "uuid": "u-123456",
      "name": "Dr. John Smith",
      "student_id": "",
      "phone_number": "+2348031234567",
      "telegram_id": "",
      "role": "lecturer"
    }
  ]
  ```
* **Response**: `{"status": "success", "count": 1}`

### 2. Get Schedules
* **Endpoint**: `GET /api/get_schedules?since=<unix_timestamp>`
* **Response**:
  ```json
  [
    {
      "course_code": "CSC301",
      "course_title": "Database Systems",
      "start_time": 1781203200,
      "end_time": 1781210400
    }
  ]
  ```

---

## Telegram Bot Interactions

Add the bot on Telegram: `@MyUniAttendance_Bot`

### 1. Verification Flow
1. Send `/start` to the bot.
2. If your phone number is not yet linked, the bot will ask you to share your contact card using the **"Share Contact Details"** button.
3. The bot automatically normalizes and matches the last 9 digits of your phone number against the database of synced users.
4. Once verified, your `telegram_id` is linked to your account, and lecturer features are unlocked.

*Note for Developers:* If you are testing locally and want to bypass physical device syncing, send `/auth_me_dev` to automatically register yourself as a test Lecturer.

### 2. Scheduling a Lecture
1. Send `/schedule` (or start from `/start`).
2. Provide the details prompted:
   - **Course Code** (e.g., `CSC301`)
   - **Course Title** (e.g., `Database Systems`)
   - **Date** (Format: `DD/MM/YYYY`)
   - **Start Time** (Format: `HH:MM`, 24h clock)
   - **End Time** (Format: `HH:MM`, 24h clock)
3. The bot will save the schedule and confirm. The offline device will retrieve it on its next sync.

### 3. Additional Commands
- `/status` — View your registration name, role, and linked metadata.
- `/cancel` — Stop the current scheduling wizard.

---

## Free Cloud Deployment on Render (Recommended)

This bot is designed specifically for Render's **Free Web Service** tier. Render will spin the container down after 15 minutes of HTTP inactivity. The key insight is that Telegram communicates through **webhooks** — every time a lecturer sends a message, Telegram makes an HTTP POST to your Render URL, which instantly wakes the container (usually in 1–3 seconds). The lecturer never notices the cold start.

### How the Webhook Sleep-Wake Cycle Works

```
Lecturer sends /start
    → Telegram sends HTTP POST → https://your-app.onrender.com/telegram_webhook
        → Render wakes container (1–3 seconds)
            → bot.py processes the update
                → Sends reply to lecturer
Lecturer is idle for 15 min → Render spins container down
    → No compute cost while sleeping
```

The bot automatically detects whether it's running on Render (via the `RENDER_EXTERNAL_URL` environment variable) and switches between webhook mode (Render) and polling mode (local dev).

---

### Step-by-Step Render Deployment

#### 1. Push Your Code to GitHub
Ensure the `telegram_bot/` directory is in a GitHub repository:
```bash
git add telegram_bot/
git commit -m "Add Telegram Bot and API server"
git push
```

#### 2. Create a Render Account
Sign up for free at [render.com](https://render.com).

#### 3. Create a New Web Service
1. Click **New +** → **Web Service**
2. Connect your GitHub account and select your repository
3. Configure the service:

| Setting | Value |
|---|---|
| **Name** | `myuni-attendance-bot` (or anything) |
| **Region** | Frankfurt (or closest) |
| **Branch** | `main` |
| **Root Directory** | `telegram_bot` |
| **Runtime** | `Python 3` |
| **Build Command** | `pip install -r requirements.txt` |
| **Start Command** | `python bot.py` |
| **Instance Type** | **Free** |

#### 4. Deploy
Click **Create Web Service**. Render will build and deploy automatically.

#### 5. Copy Your Service URL
Once deployed, Render shows your URL in the dashboard (e.g., `https://myuni-attendance-bot.onrender.com`). 

The bot automatically registers its webhook with Telegram on startup using the `RENDER_EXTERNAL_URL` environment variable that Render injects.

#### 6. Configure the Device
Copy your Render URL and enter it in the **Settings → Cloud Endpoint URL** field on the ESP32-P4 device. The device will sync to:
- `POST https://myuni-attendance-bot.onrender.com/api/sync_users`
- `GET  https://myuni-attendance-bot.onrender.com/api/get_schedules`

---

### Verifying the Deployment

1. Open Telegram and search for `@MyUniAttendance_Bot`
2. Send `/start` — the bot should reply within a few seconds (even after sleeping)
3. Send `/auth_me_dev` to create a test lecturer account
4. Send `/schedule` and walk through the 5-step wizard

Check your service logs in the Render dashboard to confirm webhook events are being received.
