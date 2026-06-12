# Hosting the Smart Attendance Bot on Render

This guide provides step-by-step instructions for deploying the cloud Telegram bot backend on [Render.com](https://render.com/).

## Prerequisites
1. A [GitHub](https://github.com/) account.
2. A [Render](https://render.com/) account.
3. A Telegram Bot Token from [@BotFather](https://t.me/BotFather).

---

## Step 1: Push Project to GitHub
Ensure the codebase is pushed to your GitHub repository (refer to the Git instructions in the root directory).

---

## Step 2: Create a Web Service on Render
1. Log in to your **Render Dashboard** and click **New > Web Service**.
2. Connect your GitHub repository containing the **SmartAttendance** code.
3. Configure the following fields:
   - **Name**: `smart-attendance-bot` (or any custom name)
   - **Runtime**: `Python 3`
   - **Branch**: `master` (or your active branch)
   - **Root Directory**: Leave blank (runs from the repository root)
   - **Build Command**: `pip install -r telegram_bot/requirements.txt`
   - **Start Command**: `python telegram_bot/bot.py`
   - **Instance Type**: Select the **Free** tier.

---

## Step 3: Add a Persistent Disk (Crucial)
Render's free tier has an ephemeral filesystem, meaning your SQLite database will be wiped every time the service restarts. To persist users and schedules:
1. In the Web Service settings, go to the **Disk** section.
2. Click **Add Disk**.
3. Configure:
   - **Name**: `bot-db`
   - **Mount Path**: `/data`
   - **Size**: `1 GiB` (fully covered under free tier limits)

---

## Step 4: Configure Environment Variables
Go to the **Environment** tab of your Web Service and add the following environment variables:

| Key | Value | Description |
|---|---|---|
| `PORT` | `10000` | Render port (bot fallback defaults to 8000, Render overrides this) |
| `DATABASE_PATH` | `/data/bot_data.db` | Redirects SQLite to write to our mounted persistent disk |
| `TELEGRAM_TOKEN` | `your_bot_token_here` | The token obtained from @BotFather |

*Note: The bot's code automatically detects the `RENDER_EXTERNAL_URL` environment variable provided by Render to set up the Telegram Webhook. This allows the bot to sleep when inactive and wake up instantly when messages are received.*

---

## Step 5: Update Device Sync Configuration
Once deployment completes, Render will provide a public URL (e.g. `https://smart-attendance-bot.onrender.com`).
To link your device:
1. Open the captive portal or the **Settings** screen on the ESP32 device.
2. Update the **Cloud Server URL / API Endpoint** to point to your Render URL (e.g., `https://smart-attendance-bot.onrender.com`).
3. Press **Save** to apply the new endpoint.
