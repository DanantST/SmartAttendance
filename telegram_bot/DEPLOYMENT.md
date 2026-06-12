# Hosting the Smart Attendance Bot on Google Cloud Engine (Always Free)

This guide provides instructions for deploying the cloud Telegram bot backend on a free **Google Compute Engine (GCE) e2-micro** instance. This setup runs the bot in long-polling mode (meaning no web server or exposed ports are required) and persists the SQLite database directly on the VM's persistent storage.

---

## Google Cloud "Always Free" Tier Benefits
Google Cloud offers a free tier that includes:
1. **1 `e2-micro` VM instance** per month (24/7 runtime).
2. **30 GB** of Standard Persistent Disk (HDD) storage.
3. Free external IP address.
*(Note: To qualify for the free tier, you must choose a VM in US regions: **Oregon (us-west1)**, **Iowa (us-central1)**, or **South Carolina (us-east1)**).*

---

## Step 1: Create the VM Instance in Google Cloud

1. Log in to the [Google Cloud Console](https://console.cloud.google.com/).
2. Navigate to **Compute Engine > VM Instances** and click **Create Instance**.
3. Configure the following fields exactly to qualify for the Free Tier:
   *   **Name:** `smart-attendance-bot`
   *   **Region:** Select either `us-central1` (Iowa), `us-west1` (Oregon), or `us-east1` (South Carolina).
   *   **Zone:** Any zone in the selected region.
   *   **Machine configuration:**
       *   **Series:** `E2`
       *   **Machine type:** `e2-micro` (2 vCPUs, 1 GB RAM).
   *   **Boot Disk:** Click **Change** and configure:
       *   **Operating System:** `Ubuntu`
       *   **Version:** `Ubuntu 24.04 LTS` (or `Ubuntu 22.04 LTS`)
       *   **Boot disk type:** **Standard Persistent Disk** (HDD) — *Do NOT select SSD or Balanced SSD*.
       *   **Size (GB):** `30` (maximum free tier limit).
       *   Click **Select**.
   *   **Firewall:** Leave both HTTP and HTTPS traffic options **unchecked** (unexposed and secure).
4. Click **Create** at the bottom of the page.

---

## Step 2: Access the VM Terminal via SSH

1. Once the instance status shows a green checkmark, look at the instance row on the dashboard.
2. Click the **SSH** button in the "Connect" column. This opens a secure terminal window inside your browser.

---

## Step 3: Install Python and Clone the Codebase

Inside the browser SSH terminal, run the following commands to install dependencies:

```bash
# 1. Update system package index
sudo apt update && sudo apt upgrade -y

# 2. Install Git, Python3, and Virtualenv package
sudo apt install -y git python3-pip python3-venv
```

Now, clone your GitHub repository:

```bash
# 3. Clone repository (replace with your repository link if different)
git clone https://github.com/DanantST/SmartAttendance.git
cd SmartAttendance
```

---

## Step 4: Set Up Virtual Environment & Environment Variables

Create the Python virtual environment and install requirements:

```bash
# 1. Initialize virtual environment
python3 -m venv .venv
source .venv/bin/activate

# 2. Install Python dependencies
pip install -r telegram_bot/requirements.txt
```

Create a production environment file:

```bash
# 3. Create a .env configuration file
nano telegram_bot/.env
```

Paste the following variables (replacing the token with your own token from `@BotFather`):

```env
TELEGRAM_BOT_TOKEN=your_bot_token_here
START_WEB_SERVER=false
DATABASE_PATH=telegram_bot/bot_data.db
```
*Press `Ctrl+O` then `Enter` to save, and `Ctrl+X` to exit nano.*

---

## Step 5: Configure the Bot as a Persistent Background Service

To keep the bot running 24/7, even after you close the SSH terminal or if the VM restarts, register it as a system service:

```bash
# 1. Create a systemd service definition file
sudo nano /etc/systemd/system/telegram-bot.service
```

Paste the following configuration:

```ini
[Unit]
Description=Telegram Attendance Bot Background Service
After=network.target

[Service]
Type=simple
User=ubuntu
WorkingDirectory=/home/ubuntu/SmartAttendance
EnvironmentFile=/home/ubuntu/SmartAttendance/telegram_bot/.env
ExecStart=/home/ubuntu/SmartAttendance/.venv/bin/python telegram_bot/bot.py
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```
*Press `Ctrl+O` then `Enter` to save, and `Ctrl+X` to exit nano.*

Now enable and start the service:

```bash
# 2. Reload daemon configuration
sudo systemctl daemon-reload

# 3. Enable the service to launch automatically on system boot
sudo systemctl enable telegram-bot.service

# 4. Start the service
sudo systemctl start telegram-bot.service
```

---

## Step 6: Verify Service Status and Logs

You can monitor your bot's execution logs in real-time with:

```bash
sudo journalctl -u telegram-bot.service -f
```

It should show:
```text
db - INFO - Database initialized successfully.
__main__ - INFO - Initializing Telegram Bot...
telegram.ext.Application - INFO - Application started
__main__ - INFO - No public URL detected, starting bot in polling mode...
__main__ - INFO - Telegram Bot polling started.
__main__ - INFO - Web server disabled (START_WEB_SERVER=false). Running bot in polling mode only.
```

Your cloud bot is now successfully deployed and fully operational!
