# Cloud Deployment Guide (Hugging Face Spaces)

This guide provides step-by-step instructions for deploying the cloud Telegram bot backend on **Hugging Face Spaces** (Docker SDK) for free, with no credit card required.

This setup runs the bot in **Webhook mode** using a FastAPI web server on Hugging Face's exposed port (`7860`). This allows the bot to wake up instantly on any incoming message even if Hugging Face puts the container to sleep after 1 hour of inactivity.

---

## Why Hugging Face Spaces?
1. **100% Free:** Basic CPU spaces are free and do **not** require any credit card registration.
2. **Auto-Sleep Compatible:** Webhook mode ensures the container automatically boots up whenever Telegram sends a message or when the physical ESP32 device pings the API.
3. **Environment Secrets:** Your Telegram Token is securely injected using Hugging Face Space secrets.

---

## Step 1: Create a Space on Hugging Face

1. Go to [huggingface.co](https://huggingface.co/) and sign up or log in.
2. Click **New Space** (or go to [huggingface.co/new-space](https://huggingface.co/new-space)).
3. Configure the Space:
   *   **Space Name:** `smart-attendance-bot` (or any custom name)
   *   **SDK:** Select **Docker** (very important).
   *   **Docker Template:** Select **Blank**.
   *   **Space Hardware:** Select **CPU basic (Free)**.
   *   **Space Visibility:** Set to **Public** or **Private**.
4. Click **Create Space**.

---

## Step 2: Inject Your Environment Secrets

1. Go to the **Settings** tab of your newly created Space.
2. Scroll to the **Variables and secrets** section.
3. Click **New secret** to add your variables:
   
   | Name | Value | Description |
   | :--- | :--- | :--- |
   | `TELEGRAM_BOT_TOKEN` | `your_bot_token_here` | Obtained from `@BotFather` |
   | `PUBLIC_URL` | `https://YOUR_USERNAME-YOUR_SPACE_NAME.hf.space` | **Your public Hugging Face URL** (replace `YOUR_USERNAME` and `YOUR_SPACE_NAME` with your actual details, using hyphens instead of slashes. E.g. `https://danantst-smart-attendance-bot.hf.space`). This activates Webhook mode. |
   
4. Click **Save** for each secret.

---

## Step 3: Configure GitHub Actions Auto-Sync

Every push you make to your GitHub `master` branch will automatically build and deploy your Hugging Face Space:

1. **Create Hugging Face Write Token:** Go to [huggingface.co/settings/tokens](https://huggingface.co/settings/tokens), click **New token**, name it `github-sync`, select the **Write** permission type, and copy the token.
2. **Add to GitHub Secrets:**
   * Go to your GitHub repository -> **Settings > Secrets and variables > Actions > New repository secret**.
   * Name: **`HF_TOKEN`**
   * Value: *Paste the token you copied from Hugging Face.*
3. **Push / Sync:** The next push to GitHub will automatically trigger the action and deploy your code to Hugging Face.

---

## Step 4: Verify the Space is Running

1. Once the build completes and shows a green **Running** badge, click your Space's public URL:
   `https://YOUR_USERNAME-YOUR_SPACE_NAME.hf.space`
2. It should respond with:
   `{"status":"ok","message":"Smart Attendance Bot is running"}`
3. You can monitor execution logs in the **Logs** tab of your Space page.

---

## Step 5: Update Device Sync Configuration

To connect your physical attendance device to the cloud bot:
1. Turn on the ESP32-P4 device.
2. Open the captive portal or the **Settings** screen on the device.
3. Update the **Cloud Server URL / API Endpoint** to point to your Hugging Face Space URL (e.g. `https://danantst-smart-attendance-bot.hf.space`).
4. Press **Save** on the device to apply.
