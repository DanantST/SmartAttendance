FROM python:3.11-slim

# Install system dependencies (including tzdata for timezone configuration)
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential \
    tzdata \
    && rm -rf /var/lib/apt/lists/*

# Set process and system timezone to Africa/Lagos
ENV TZ=Africa/Lagos
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

# Set working directory
WORKDIR /app

# Copy requirements and install
COPY telegram_bot/requirements.txt requirements.txt
RUN pip install --no-cache-dir -r requirements.txt

# Copy application files
COPY telegram_bot /app/telegram_bot

# Create data directory for SQLite database and set permissions for HF Spaces user (UID 1000)
RUN mkdir -p /app/data && chmod 777 /app/data

# Set environment variables
ENV PORT=7860
ENV START_WEB_SERVER=true
ENV DATABASE_PATH=/app/data/bot_data.db

# Expose standard Hugging Face Space port
EXPOSE 7860

# Run the application
CMD ["python", "telegram_bot/bot.py"]
