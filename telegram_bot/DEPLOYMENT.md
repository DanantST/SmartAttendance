# Hosting the Smart Attendance Bot on Hugging Face Spaces (Docker)

This guide provides instructions for deploying the cloud Telegram bot backend on **Hugging Face Spaces** using the provided `Dockerfile`. 

This setup runs the bot in **long-polling** mode, starts a lightweight FastAPI web server on Hugging Face's required port (`7860`), exposes a health-check endpoint to keep the container active, and stores the SQLite database in a writable directory.

---

## Why Hugging Face Spaces?
1. **Strictly Free:** Basic CPU spaces are 100% free and do **not** require entering credit card or payment information.
2. **Indefinite Uptime:** The bot runs continuously as long as the exposed health check responds on port `7860`.
3. **Environment Secrets:** Your Telegram Token is securely injected and hidden from public view using Hugging Face Space secrets.

---

## Step 1: Create a Space on Hugging Face

1. Go to [huggingface.co](https://huggingface.co/) and sign up or log in.
2. Click on your profile picture in the top-right corner and select **New Space** (or go to [huggingface.co/new-space](https://huggingface.co/new-space)).
3. Configure the Space:
   *   **Space Name:** `smart-attendance-bot` (or any custom name)
   *   **License:** `mit` (or leave blank)
   *   **SDK:** Select **Docker** (very important).
   *   **Docker Template:** Select **Blank** (this will automatically build using the `Dockerfile` at the root of our repository).
   *   **Space Hardware:** Select **CPU basic • 2 vCPU • 16 GB • Free** (default).
   *   **Space Visibility:** Set to **Public** or **Private** (we store the bot token in secrets, so it's safe either way).
4. Click **Create Space**.

---

## Step 2: Inject Your Environment Secrets

1. Once the Space is created, click the **Settings** tab at the top-right of your Space's page.
2. Scroll down to the **Variables and secrets** section.
3. Click **New secret** and add your Telegram token:
   *   **Name:** `TELEGRAM_BOT_TOKEN`
   *   **Value:** `your_actual_bot_token_here` (obtained from `@BotFather`)
4. Click **Save**.

---

## Step 3: Deploy Your Code to Hugging Face

There are two easy methods to deploy the code to your Hugging Face Space:

### Method A: Direct Git Push (Easiest)
In your local terminal inside the repository, add the Hugging Face Space as a remote and push your code:

```bash
# 1. Add Hugging Face Space remote (replace USERNAME and SPACE_NAME with yours)
git remote add huggingface https://huggingface.co/spaces/YOUR_USERNAME/YOUR_SPACE_NAME

# 2. Push to Hugging Face
git push huggingface master --force
```

### Method B: GitHub Actions Auto-Sync
If you prefer pushing only to GitHub and having Hugging Face sync automatically:
1. Create a **Hugging Face Write Token** by going to your HF profile -> **Settings > Access Tokens** -> click **New token** (type: **Write**).
2. Go to your GitHub repository -> **Settings > Secrets and variables > Actions** -> Click **New repository secret**.
   *   **Name:** `HF_TOKEN`
   *   **Value:** `your_hf_access_token`
3. Add a GitHub Action workflow file in your repository at `.github/workflows/hf_sync.yml`:
   ```yaml
   name: Sync to Hugging Face Spaces
   on:
     push:
       branches: [master]
   jobs:
     sync:
       runs-on: ubuntu-latest
       steps:
         - uses: actions/checkout@v3
           with:
             fetch-depth: 0
             lfs: true
         - name: Push to HF
           env:
             HF_TOKEN: ${{ secrets.HF_TOKEN }}
           run: git push --force https://YOUR_USERNAME:$HF_TOKEN@huggingface.co/spaces/YOUR_USERNAME/YOUR_SPACE_NAME master
   ```

---

## Step 4: Verify the Deployment

1. Go to your Space homepage on Hugging Face. You should see the status transition to **Building**, then **Running**.
2. Once the status shows **Running**, click the "Embed" or "App" link, or append `.hf.space` to the URL.
3. Open the public endpoint: `https://YOUR_USERNAME-YOUR_SPACE_NAME.hf.space`
4. It should respond with:
   ```json
   {"status":"ok","message":"Smart Attendance Bot is running"}
   ```
5. You can view the real-time bot execution logs by clicking the **Logs** tab at the top of your Space page.

---

## Step 5: Update Device Sync Configuration

To connect your physical attendance device to the cloud bot:
1. Turn on the ESP32-P4 device.
2. Open the captive portal or the **Settings** screen on the device.
3. Update the **Cloud Server URL / API Endpoint** to point to your Hugging Face Space URL:
   `https://YOUR_USERNAME-YOUR_SPACE_NAME.hf.space`
4. Press **Save** on the device settings to apply the configuration.
