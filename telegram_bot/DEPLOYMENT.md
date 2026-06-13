# Cloud Deployment Guide (Koyeb & Hugging Face Spaces)

This guide provides step-by-step instructions for hosting the cloud Telegram bot backend.

You can choose between two free hosting targets:
1. **Koyeb (Recommended):** 100% free (no credit card required on the Hobby plan), avoids Telegram's IP blocklists, and runs the bot alongside the FastAPI web server.
2. **Hugging Face Spaces:** Free (no credit card), but may experience `ConnectTimeout` errors because Telegram blocks AWS IP addresses.

---

## Target A: Deploying on Koyeb (Recommended)

Koyeb runs your Docker container as a free Web Service. Because Koyeb does not run on AWS, its IP addresses are **not** blocked by Telegram, ensuring your bot can connect and long-poll without timeouts.

### Step 1: Sign Up on Koyeb
1. Go to [koyeb.com](https://www.koyeb.com/) and click **Sign Up**.
2. Create an account and select the **Hobby** plan (which is free and does **not** require a credit card).

### Step 2: Create a New Service
1. On your Koyeb Dashboard, click **Create Service**.
2. Select **GitHub** as the deployment method and authorize your GitHub account.
3. Select your `SmartAttendance` repository from the list.

### Step 3: Configure Deployment Fields
In the configuration screen, set the following options:
*   **Service Type:** **Web Service** (default).
*   **Builder:** Select **Docker** (Koyeb will automatically detect the `Dockerfile` at the root of our repository).
*   **Ports:** 
    *   Set the port number to **`8000`** (change from the default 80 if needed) and protocol to **HTTP**.
*   **Environment Variables:** Click **Add Variable** to configure:
    
    | Key | Value | Description |
    | :--- | :--- | :--- |
    | `TELEGRAM_BOT_TOKEN` | `your_bot_token_here` | Your token from `@BotFather` |
    | `START_WEB_SERVER` | `true` | Starts the web server (required for Webhooks and Koyeb's health checks) |
    | `PORT` | `8000` | Overrides the internal container port to match Koyeb's routing |
    | `DATABASE_PATH` | `/app/data/bot_data.db` | Path to the SQLite database |
    | `PUBLIC_URL` | `https://your-app-name.koyeb.app` | **Your Koyeb app URL** (enables Webhook mode so Telegram wakes up the bot on any new message) |

### Step 4: Deploy the Service
1. Click **Deploy** at the bottom of the page.
2. The service will build and transition to **Healthy**.
3. Koyeb will provide a public URL (e.g. `https://smart-attendance-bot-yourname.koyeb.app`). Ensure you copy this URL and add it to your `PUBLIC_URL` environment variable in the service settings (or redeploy after launch to set it).
4. Set this URL as the cloud server endpoint in your ESP32-P4 device settings!

> [!NOTE]
> **Koyeb Scale-to-Zero Behavior:** 
> Since Koyeb Free Instances sleep (scale to zero) after 1 hour of inactivity, configuring the `PUBLIC_URL` is important. This enables **Telegram Webhook** mode. When a user sends a message, Telegram makes a POST request to your webhook, which immediately wakes up the Koyeb container to process the message. When the ESP32 device pings the `/api/` endpoints, it will also wake the service automatically.

---

## Target B: Deploying on Hugging Face Spaces (Docker)

Hugging Face Spaces is another free hosting option (no credit card required), but is prone to connection timeouts since Telegram frequently blocks AWS IP addresses.

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

*Note: If the Space logs show a `ConnectTimeout` to `api.telegram.org`, Telegram's DDoS firewall has blocked the AWS IP address assigned to your Hugging Face container. Restarting the Space (to get a new IP) or switching to Koyeb will resolve this.*
