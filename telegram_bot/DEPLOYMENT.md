# Cloud Deployment Guide (Fly.io & Hugging Face Spaces)

This guide provides step-by-step instructions for hosting the cloud Telegram bot. 

You can choose between two free hosting targets:
1. **Fly.io (Recommended):** Fully persistent, free 3GB storage volume, and avoids the IP address blocklists that Telegram imposes on AWS (Hugging Face).
2. **Hugging Face Spaces:** 100% free (no credit card required), but has ephemeral storage and may experience TLS/connection timeouts when communicating with Telegram due to AWS IP blocks.

---

## Target A: Deploying on Fly.io (Recommended)

Fly.io runs your Docker container in a micro-VM. It provides a free tier with 3 shared-CPU VMs and 3 GB of persistent disk volume, which is perfect for securing your SQLite database.

### Step 1: Install the Fly.io CLI (flyctl)
Open your terminal (PowerShell on Windows) and run:
```powershell
iwr https://fly.io/install.ps1 -useb | iex
```
*(Restart your terminal window after installation so the `fly` command is added to your PATH).*

### Step 2: Log In or Sign Up
Authenticate your account:
```bash
fly auth login
```
*(Note: Fly.io requires a credit card to verify your identity to prevent spam, but you will not be charged under their free tier).*

### Step 3: Launch the Application
Run the launch utility from the root of the project:
```bash
fly launch
```
During the prompt:
1. **An existing fly.toml was found. Copy config?** Type `y` (Yes).
2. **Would you like to set up a Postgres database / Redis?** Type `n` (No).
3. **Would you like to deploy now?** Type `n` (No) — *we must create our storage volume and token secret first.*

### Step 4: Create a Persistent Storage Volume
To ensure your SQLite database persists across redeploys, create a 1 GB volume (replace `bos` with the region you selected during `fly launch`):
```bash
fly volumes create bot_db_volume --size 1 --region bos
```

### Step 5: Inject Your Bot Token
Inject your Telegram Bot Token securely into the environment variables:
```bash
fly secrets set TELEGRAM_BOT_TOKEN="your_actual_bot_token_here"
```

### Step 6: Deploy to Fly.io
Deploy your application:
```bash
fly deploy
```
Once deployed, Fly.io will give you a public URL (e.g. `https://smart-attendance-bot.fly.dev`). Set this URL in your physical ESP32-P4 device settings!

---

## Target B: Deploying on Hugging Face Spaces (Docker)

If you do not have a credit card to verify a Fly.io account, you can use Hugging Face Spaces.

### Step 1: Create a Space on Hugging Face
1. Log in to [huggingface.co](https://huggingface.co/) and click **New Space**.
2. Set **Space Name** to `smart-attendance-bot`.
3. Select **Docker** as the SDK and choose the **Blank** template.
4. Keep the hardware as **CPU basic (Free)** and click **Create Space**.

### Step 2: Inject Your Bot Token
1. Go to your Space **Settings** tab.
2. Scroll to **Variables and secrets** and click **New secret**:
   *   **Name:** `TELEGRAM_BOT_TOKEN`
   *   **Value:** `your_actual_bot_token_here`

### Step 3: Configure GitHub Actions Auto-Sync
If you set up the GitHub secret (`HF_TOKEN` containing your Hugging Face write token), every push to your GitHub `master` branch will automatically build and deploy your Space.

*Note: If the Space logs show a `ConnectTimeout` to `api.telegram.org`, Telegram's DDoS firewall has temporarily blocked the AWS IP address assigned to your Hugging Face container. Restarting the Space (to assign a new IP) or switching to Fly.io will resolve this.*
