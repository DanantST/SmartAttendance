# Hosting the Smart Attendance Bot on Render

This guide provides step-by-step instructions for deploying the cloud Telegram bot backend on [Render.com](https://render.com/).

You can deploy the bot in one of two configurations:
1. **Background Worker (Recommended)**: Runs the bot in long polling mode. This is the simplest option and avoids setting up webhooks, but requires the Render Background Worker service type.
2. **Web Service**: Runs the bot using a web server with Telegram Webhooks.

---

## Prerequisites
1. A [GitHub](https://github.com/) account.
2. A [Render](https://render.com/) account.
3. A Telegram Bot Token from [@BotFather](https://t.me/BotFather).

---

## Step 1: Push Project to GitHub
Ensure your codebase is pushed to your GitHub repository.

---

## Step 2: Create the Service on Render

### Option A: Deploy as a Background Worker (Recommended)
1. Log in to your **Render Dashboard** and click **New > Background Worker**.
2. Connect your GitHub repository.
3. Configure the following fields:
   - **Name**: `smart-attendance-bot-worker`
   - **Runtime**: `Python 3`
   - **Branch**: `master` (or your active branch)
   - **Build Command**: `pip install -r telegram_bot/requirements.txt`
   - **Start Command**: `python telegram_bot/bot.py`

### Option B: Deploy as a Web Service
1. Log in to your **Render Dashboard** and click **New > Web Service**.
2. Connect your GitHub repository.
3. Configure the following fields:
   - **Name**: `smart-attendance-bot-web`
   - **Runtime**: `Python 3`
   - **Branch**: `master` (or your active branch)
   - **Build Command**: `pip install -r telegram_bot/requirements.txt`
   - **Start Command**: `python telegram_bot/bot.py`
   - **Instance Type**: Select the **Free** tier.

---

## Step 3: Add a Persistent Disk (Crucial)
Render's filesystem is ephemeral, meaning your SQLite database will be wiped every time the service restarts. To persist users and schedules:
1. Go to the **Disk** section in your service settings.
2. Click **Add Disk**.
3. Configure:
   - **Name**: `bot-db`
   - **Mount Path**: `/data`
   - **Size**: `1 GiB` (fully covered under free tier limits)

---

## Step 4: Configure Environment Variables
Go to the **Environment** tab of your service and add the following environment variables:

| Key | Value | Description |
|---|---|---|
| `TELEGRAM_BOT_TOKEN` | `your_bot_token_here` | The token obtained from @BotFather (replaces the hardcoded token) |
| `DATABASE_PATH` | `/data/bot_data.db` | Redirects SQLite to write to our mounted persistent disk |
| `START_WEB_SERVER` | `false` or `true` | **Set to `false` for Background Worker** to disable the Uvicorn web server and run polling. **Set to `true` (or leave unset) for Web Service** to start the web server for webhooks and device sync. |
| `PORT` | `10000` | (Web Service only) Render port for webhooks |

---

## Step 5: Update Device Sync Configuration
If running as a **Web Service**, Render will provide a public URL (e.g. `https://smart-attendance-bot-web.onrender.com`).
To link your device:
1. Open the captive portal or the **Settings** screen on the ESP32 device.
2. Update the **Cloud Server URL / API Endpoint** to point to your Render URL (e.g., `https://smart-attendance-bot-web.onrender.com`).
3. Press **Save** to apply the new endpoint.
