import logging
import asyncio
import datetime
import time
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
import uvicorn
from telegram import Update, ReplyKeyboardMarkup, KeyboardButton, ReplyKeyboardRemove
from telegram.ext import (
    Application,
    ApplicationBuilder,
    CommandHandler,
    MessageHandler,
    ConversationHandler,
    ContextTypes,
    filters,
)
import db

# Setup Logging
logging.basicConfig(
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s", level=logging.INFO
)
logger = logging.getLogger(__name__)

# Telegram Bot Token
BOT_TOKEN = "8315939667:AAHfJeTBFy5i7K2HFgQ9gpzRn7qeofEXOV0"

# Conversation States for Scheduling
COURSE_CODE, COURSE_TITLE, DATE, START_TIME, END_TIME = range(5)

# Conversation States for Course Enrollment
ENROLL_CODE = 5

# Conversation States for Unenrollment
UNENROLL_STUDENT_ID, UNENROLL_CONFIRM = range(6, 8)

# FastAPI Application
web_app = FastAPI()

# Global Bot Application Reference
bot_app = None

# ----------------- FastAPI REST Endpoints -----------------

@web_app.post("/telegram_webhook")
async def telegram_webhook(request: Request):
    """
    Receives updates from Telegram and feeds them to the bot application queue.
    This enables Render to sleep when inactive and wake up instantly when users send messages.
    """
    global bot_app
    if not bot_app:
        return JSONResponse(status_code=500, content={"status": "error", "message": "Bot not initialized"})
    try:
        data = await request.json()
        update = Update.de_json(data, bot_app.bot)
        await bot_app.update_queue.put(update)
        return JSONResponse(status_code=200, content={"status": "ok"})
    except Exception as e:
        logger.error(f"Error handling Telegram webhook: {e}")
        return JSONResponse(status_code=400, content={"status": "error", "message": str(e)})


@web_app.post("/api/sync_users")
async def sync_users(request: Request):
    """
    Accepts JSON list of users from the device and upserts them in the bot database.
    """
    try:
        users = await request.json()
        logger.info(f"Received sync request for {len(users)} users from device.")
        
        received_uuids = set()
        for user in users:
            uuid = user.get("uuid")
            name = user.get("name", "")
            student_id = user.get("student_id", "")
            phone_number = user.get("phone_number", "")
            telegram_id = user.get("telegram_id", "")
            role = user.get("role", "student")
            
            if uuid:
                received_uuids.add(uuid)
                db.upsert_user(uuid, name, student_id, phone_number, telegram_id, role)
        
        # Reconcile: delete users on the bot database that were deleted from the device
        db.reconcile_users(received_uuids)
        
        return JSONResponse(status_code=200, content={"status": "success", "count": len(users)})
    except Exception as e:
        logger.error(f"Error syncing users: {e}")
        return JSONResponse(status_code=400, content={"status": "error", "message": str(e)})

@web_app.get("/api/get_schedules")
async def get_schedules(since: int = 0):
    """
    Returns new schedules created via Telegram bot since the specified timestamp.
    """
    try:
        logger.info(f"Device requesting schedules created since: {since}")
        schedules = db.get_schedules_since(since)
        logger.info(f"Returning {len(schedules)} schedules to device.")
        return JSONResponse(status_code=200, content=schedules)
    except Exception as e:
        logger.error(f"Error fetching schedules: {e}")
        return JSONResponse(status_code=500, content={"status": "error", "message": str(e)})


@web_app.get("/api/get_course_enrollments")
async def get_course_enrollments(since: int = 0):
    """
    Returns a list of {user_uuid, course_code} pairs that were created since
    the given timestamp. The device calls this during its cloud sync cycle and
    calls db_link_user_course() for each entry to build its local user_courses table.
    """
    try:
        enrollments = db.get_enrollments_since(since)
        logger.info(f"Returning {len(enrollments)} course enrollments to device.")
        return JSONResponse(status_code=200, content=enrollments)
    except Exception as e:
        logger.error(f"Error fetching enrollments: {e}")
        return JSONResponse(status_code=500, content={"status": "error", "message": str(e)})


@web_app.get("/api/get_deletions")
async def get_deletions(since: int = 0):
    """
    Returns a list of {uuid} of deleted users since the specified timestamp.
    """
    try:
        deletions = db.get_deletions_since(since)
        logger.info(f"Returning {len(deletions)} deletions to device.")
        return JSONResponse(status_code=200, content=[{"uuid": uuid} for uuid in deletions])
    except Exception as e:
        logger.error(f"Error fetching deletions: {e}")
        return JSONResponse(status_code=500, content={"status": "error", "message": str(e)})

# ----------------- Telegram Bot Handlers -----------------

async def start_cmd(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Greets the user and checks registration status. Requests contact sharing if unregistered.
    """
    user_id = str(update.effective_user.id)
    user = db.get_user_by_telegram_id(user_id)
    
    if user:
        role = user.get("role")
        name = user.get("name")
        if role in ["lecturer", "admin"]:
            await update.message.reply_text(
                f"Welcome back, {name}! You are registered as a **{role.capitalize()}**.\n\n"
                "Available commands:\n"
                "/schedule - Start scheduling a lecture\n"
                "/unenroll - Unenroll a student from the system\n"
                "/cancel - Abort current conversation\n"
                "/status - Check your registration details"
            )
        else:
            await update.message.reply_text(
                f"Hello {name}, you are registered as a **Student**.\n"
                "You will receive automatic notifications when your attendance is recorded.\n\n"
                "📚 Use /enroll_course to register for courses you are taking."
            )
    else:
        # Prompt to share phone number to link accounts
        contact_keyboard = ReplyKeyboardMarkup(
            [[KeyboardButton(text="Share Contact Details", request_contact=True)]],
            one_time_keyboard=True,
            resize_keyboard=True,
        )
        await update.message.reply_text(
            "Welcome to the Uni Attendance Bot!\n\n"
            "To pair this account with the Smart Attendance system, "
            "please click the button below to share your phone number.",
            reply_markup=contact_keyboard,
        )

async def contact_handler(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Handles contact sharing, matches the phone number, and updates the Telegram ID.
    """
    contact = update.message.contact
    if not contact:
        await update.message.reply_text("Invalid contact details shared.")
        return

    # Check if contact is from the sender (prevent spoofing)
    if contact.user_id != update.effective_user.id:
        await update.message.reply_text("You must share your own contact card.")
        return

    phone_number = contact.phone_number
    user_id = str(update.effective_user.id)
    
    logger.info(f"Looking up phone number: {phone_number} for telegram_id: {user_id}")
    matched_user = db.find_user_by_phone(phone_number)
    
    if matched_user:
        db.link_telegram_id(matched_user["uuid"], user_id)
        role = matched_user["role"]
        name = matched_user["name"]
        
        await update.message.reply_text(
            f"Registration successful! ✅\n\n"
            f"**Name:** {name}\n"
            f"**Role:** {role.capitalize()}\n\n"
            "Your Telegram account has been linked to the attendance device.",
            reply_markup=ReplyKeyboardRemove(),
        )
        
        if role in ["lecturer", "admin"]:
            await update.message.reply_text(
                "You can now create lecture schedules using the /schedule command."
            )
    else:
        await update.message.reply_text(
            f"Sorry, your phone number ({phone_number}) is not registered on the attendance device.\n"
            "Please register on the captive Web AP portal first, then try /start again.",
            reply_markup=ReplyKeyboardRemove(),
        )

async def auth_me_dev_cmd(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Developer override command to force-register the current user as a lecturer for testing.
    """
    user_id = str(update.effective_user.id)
    first_name = update.effective_user.first_name or "Dev User"
    
    # Check if already registered
    existing_user = db.get_user_by_telegram_id(user_id)
    if existing_user:
        await update.message.reply_text(f"You are already registered as a {existing_user['role']}.")
        return

    uuid = f"dev-uuid-{user_id}"
    db.upsert_user(
        uuid=uuid,
        name=f"Dev Lecturer ({first_name})",
        student_id="",
        phone_number="123456789",
        telegram_id=user_id,
        role="lecturer"
    )
    
    await update.message.reply_text(
        "🛠️ Developer Override: You have been registered in the local DB as a **Lecturer**.\n"
        "You can now run /schedule to create test classes."
    )

async def status_cmd(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Returns current registration details.
    """
    user_id = str(update.effective_user.id)
    user = db.get_user_by_telegram_id(user_id)
    if user:
        await update.message.reply_text(
            f"👤 **Registration Details:**\n"
            f"**Name:** {user['name']}\n"
            f"**Role:** {user['role'].capitalize()}\n"
            f"**Phone:** {user['phone_number'] or 'Not provided'}\n"
            f"**ID:** {user['student_id'] or 'N/A'}"
        )
    else:
        await update.message.reply_text("You are not registered yet. Use /start to register.")

# ----------------- Scheduling Conversation Flow -----------------

async def schedule_start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Entry point for scheduling. Verifies user is authorized.
    """
    user_id = str(update.effective_user.id)
    user = db.get_user_by_telegram_id(user_id)
    
    if not user or user.get("role") not in ["lecturer", "admin"]:
        await update.message.reply_text(
            "❌ Unauthorized. Only registered Lecturers or Admins can schedule lectures."
        )
        return ConversationHandler.END

    await update.message.reply_text(
        "Let's schedule a lecture.\n"
        "Please enter the **Course Code** (e.g., CSC301):"
    )
    return COURSE_CODE

async def schedule_course_code(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Receives course code, prompts for course title.
    """
    course_code = update.message.text.strip().upper()
    if not course_code:
        await update.message.reply_text("Please enter a valid course code:")
        return COURSE_CODE
        
    context.user_data["course_code"] = course_code
    await update.message.reply_text(
        f"Course Code set to: {course_code}\n\n"
        "Now, please enter the **Course Title** (e.g., Database Systems):"
    )
    return COURSE_TITLE

async def schedule_course_title(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Receives course title, prompts for date.
    """
    course_title = update.message.text.strip()
    if not course_title:
        await update.message.reply_text("Please enter a valid course title:")
        return COURSE_TITLE
        
    context.user_data["course_title"] = course_title
    await update.message.reply_text(
        f"Course Title set to: {course_title}\n\n"
        "Please enter the **Date** of the lecture (Format: DD/MM/YYYY):"
    )
    return DATE

async def schedule_date(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Receives date, validates format, prompts for start time.
    """
    date_str = update.message.text.strip()
    try:
        # Validate format
        datetime.datetime.strptime(date_str, "%d/%m/%Y")
    except ValueError:
        await update.message.reply_text(
            "❌ Invalid date format. Please write it exactly as DD/MM/YYYY (e.g., 18/06/2026):"
        )
        return DATE

    context.user_data["date"] = date_str
    await update.message.reply_text(
        f"Date set to: {date_str}\n\n"
        "Please enter the **Start Time** (Format: HH:MM, 24-hour clock, e.g., 09:00):"
    )
    return START_TIME

async def schedule_start_time(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Receives start time, validates format, prompts for end time.
    """
    time_str = update.message.text.strip()
    try:
        # Validate format
        datetime.datetime.strptime(time_str, "%H:%M")
    except ValueError:
        await update.message.reply_text(
            "❌ Invalid time format. Please write it exactly as HH:MM (e.g., 09:30 or 14:00):"
        )
        return START_TIME

    context.user_data["start_time"] = time_str
    await update.message.reply_text(
        f"Start Time set to: {time_str}\n\n"
        "Please enter the **End Time** (Format: HH:MM, 24-hour clock, e.g., 11:00):"
    )
    return END_TIME

async def schedule_end_time(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Receives end time, parses timestamps, creates schedule, and finishes conversation.
    """
    end_time_str = update.message.text.strip()
    try:
        # Validate format
        datetime.datetime.strptime(end_time_str, "%H:%M")
    except ValueError:
        await update.message.reply_text(
            "❌ Invalid time format. Please write it exactly as HH:MM (e.g., 11:00 or 16:30):"
        )
        return END_TIME

    date_str = context.user_data["date"]
    start_time_str = context.user_data["start_time"]
    
    try:
        # Combine and parse into datetimes
        start_dt = datetime.datetime.strptime(f"{date_str} {start_time_str}", "%d/%m/%Y %H:%M")
        end_dt = datetime.datetime.strptime(f"{date_str} {end_time_str}", "%d/%m/%Y %H:%M")
        
        start_epoch = int(start_dt.timestamp())
        end_epoch = int(end_dt.timestamp())
        
        if end_epoch <= start_epoch:
            await update.message.reply_text(
                "❌ End time must be strictly after the start time. Please enter the End Time again:"
            )
            return END_TIME

        user_id = str(update.effective_user.id)
        course_code = context.user_data["course_code"]
        course_title = context.user_data["course_title"]
        
        db.add_schedule(user_id, course_code, course_title, start_epoch, end_epoch)
        
        await update.message.reply_text(
            "✅ **Lecture Scheduled Successfully!**\n\n"
            f"**Course:** {course_code} - {course_title}\n"
            f"**Date:** {date_str}\n"
            f"**Time:** {start_time_str} - {end_time_str}\n\n"
            "The device will fetch and sync this lecture automatically during its next cloud sync cycle."
        )
        return ConversationHandler.END

    except Exception as e:
        logger.error(f"Error parsing date/times for schedule: {e}")
        await update.message.reply_text("❌ An internal error occurred. Please try again with /schedule.")
        return ConversationHandler.END

async def cancel_cmd(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Aborts any active conversation.
    """
    await update.message.reply_text(
        "Operation cancelled.", reply_markup=ReplyKeyboardRemove()
    )
    return ConversationHandler.END


# ----------------- Course Enrollment Conversation (Students) -----------------

async def enroll_course_start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Entry point: verify user is a registered student, then ask for course code.
    """
    user_id = str(update.effective_user.id)
    user = db.get_user_by_telegram_id(user_id)

    if not user:
        await update.message.reply_text(
            "You are not registered yet. Please use /start and share your contact first."
        )
        return ConversationHandler.END

    # Show available courses for reference
    courses = db.get_all_courses()
    if courses:
        course_list = "\n".join(f"  • {c['code']} — {c['name']}" for c in courses)
        intro = (
            f"Available courses:\n{course_list}\n\n"
            "Please type the **Course Code** you want to enroll in (e.g. CS480):"
        )
    else:
        intro = "Please type the **Course Code** you want to enroll in (e.g. CS480):"

    await update.message.reply_text(intro)
    return ENROLL_CODE


async def enroll_course_code(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Receives course code, enrolls student, confirms.
    """
    code = update.message.text.strip().upper()
    if not code:
        await update.message.reply_text("Please enter a valid course code:")
        return ENROLL_CODE

    user_id = str(update.effective_user.id)
    user = db.get_user_by_telegram_id(user_id)
    if not user:
        await update.message.reply_text("Session expired. Please use /start again.")
        return ConversationHandler.END

    result = db.enroll_user_in_course(user["uuid"], code)
    if result == "already_enrolled":
        await update.message.reply_text(
            f"ℹ️ You are already enrolled in **{code}**."
        )
    elif result == "not_found":
        await update.message.reply_text(
            f"❌ Course code **{code}** was not found. Please check the code and try again:"
        )
        return ENROLL_CODE
    elif result == "ok":
        await update.message.reply_text(
            f"✅ You have been successfully enrolled in **{code}**.\n"
            "The attendance device will sync this enrollment during its next cloud sync cycle."
        )
    else:
        await update.message.reply_text("An error occurred. Please try again with /enroll_course.")

    return ConversationHandler.END

async def unenroll_start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Entry point for unenrollment. Verifies user is authorized.
    """
    user_id = str(update.effective_user.id)
    user = db.get_user_by_telegram_id(user_id)
    
    if not user or user.get("role") not in ["lecturer", "admin"]:
        await update.message.reply_text(
            "❌ Unauthorized. Only registered Lecturers or Admins can unenroll students."
        )
        return ConversationHandler.END

    await update.message.reply_text(
        "Let's unenroll a student from the system.\n"
        "Please enter the **Student ID** (Matric ID) of the student:"
    )
    return UNENROLL_STUDENT_ID

async def unenroll_student_id(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Receives Student ID, finds the student, and prompts for confirmation.
    """
    student_id = update.message.text.strip()
    if not student_id:
        await update.message.reply_text("Please enter a valid Student ID:")
        return UNENROLL_STUDENT_ID

    student = db.get_user_by_student_id(student_id)
    if not student:
        await update.message.reply_text(
            f"❌ Student with ID **{student_id}** not found.\n"
            "Please double check the ID and try again, or type /cancel to abort:"
        )
        return UNENROLL_STUDENT_ID

    context.user_data["unenroll_uuid"] = student["uuid"]
    context.user_data["unenroll_name"] = student["name"]
    context.user_data["unenroll_student_id"] = student["student_id"]

    await update.message.reply_text(
        f"⚠️ **Confirm Unenrollment** ⚠️\n\n"
        f"Are you sure you want to unenroll the following student?\n"
        f"**Name:** {student['name']}\n"
        f"**Student ID:** {student['student_id']}\n"
        f"**Phone:** {student['phone_number'] or 'N/A'}\n\n"
        f"This will delete all their data, course enrollments, and attendance logs.\n"
        f"Type **YES** to confirm or **NO** to cancel:"
    )
    return UNENROLL_CONFIRM

async def unenroll_confirm(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Receives confirmation and deletes the student if YES.
    """
    confirm = update.message.text.strip().upper()
    uuid = context.user_data.get("unenroll_uuid")
    name = context.user_data.get("unenroll_name")
    student_id = context.user_data.get("unenroll_student_id")

    if confirm == "YES":
        if db.delete_user_by_uuid(uuid):
            await update.message.reply_text(
                f"✅ **Student Successfully Unenrolled!**\n\n"
                f"**Name:** {name}\n"
                f"**Student ID:** {student_id}\n\n"
                f"The attendance device will delete this user from its local database during its next sync cycle."
            )
        else:
            await update.message.reply_text("❌ An error occurred while deleting the student. Please try again.")
        return ConversationHandler.END
    elif confirm == "NO":
        await update.message.reply_text("Operation cancelled.")
        return ConversationHandler.END
    else:
        await update.message.reply_text("Please type either **YES** or **NO**:")
        return UNENROLL_CONFIRM

# ----------------- Main Runner -----------------

async def main():
    global bot_app
    # 1. Initialize SQLite Database
    db.init_db()
    
    # 2. Build Telegram Bot Application
    bot_app = ApplicationBuilder().token(BOT_TOKEN).build()
    
    # Registration & Commands
    bot_app.add_handler(CommandHandler("start", start_cmd))
    bot_app.add_handler(CommandHandler("auth_me_dev", auth_me_dev_cmd))
    bot_app.add_handler(CommandHandler("status", status_cmd))
    bot_app.add_handler(MessageHandler(filters.CONTACT, contact_handler))

    # Course Enrollment Conversation (Students)
    enroll_conv = ConversationHandler(
        entry_points=[CommandHandler("enroll_course", enroll_course_start)],
        states={
            ENROLL_CODE: [MessageHandler(filters.TEXT & ~filters.COMMAND, enroll_course_code)],
        },
        fallbacks=[CommandHandler("cancel", cancel_cmd)],
    )
    bot_app.add_handler(enroll_conv)

    # Scheduling Conversation Handler (Lecturers/Admins)
    schedule_conv = ConversationHandler(
        entry_points=[CommandHandler("schedule", schedule_start)],
        states={
            COURSE_CODE: [MessageHandler(filters.TEXT & ~filters.COMMAND, schedule_course_code)],
            COURSE_TITLE: [MessageHandler(filters.TEXT & ~filters.COMMAND, schedule_course_title)],
            DATE: [MessageHandler(filters.TEXT & ~filters.COMMAND, schedule_date)],
            START_TIME: [MessageHandler(filters.TEXT & ~filters.COMMAND, schedule_start_time)],
            END_TIME: [MessageHandler(filters.TEXT & ~filters.COMMAND, schedule_end_time)],
        },
        fallbacks=[CommandHandler("cancel", cancel_cmd)],
    )
    bot_app.add_handler(schedule_conv)
    
    # Unenrollment Conversation Handler (Lecturers/Admins)
    unenroll_conv = ConversationHandler(
        entry_points=[CommandHandler("unenroll", unenroll_start)],
        states={
            UNENROLL_STUDENT_ID: [MessageHandler(filters.TEXT & ~filters.COMMAND, unenroll_student_id)],
            UNENROLL_CONFIRM: [MessageHandler(filters.TEXT & ~filters.COMMAND, unenroll_confirm)],
        },
        fallbacks=[CommandHandler("cancel", cancel_cmd)],
    )
    bot_app.add_handler(unenroll_conv)
    
    # Initialize Bot App
    logger.info("Initializing Telegram Bot...")
    await bot_app.initialize()
    await bot_app.start()
    
    # Check if we should use Webhook (Render) or Polling (Local Dev)
    import os
    public_url = os.environ.get("RENDER_EXTERNAL_URL")
    if public_url:
        logger.info(f"Setting Telegram webhook to: {public_url}/telegram_webhook")
        public_url = public_url.rstrip("/")
        await bot_app.bot.set_webhook(url=f"{public_url}/telegram_webhook")
    else:
        logger.info("No public URL detected, starting bot in polling mode...")
        # Clear webhook first so polling works
        await bot_app.bot.delete_webhook()
        await bot_app.updater.start_polling()
        logger.info("Telegram Bot polling started.")
    
    # 3. Configure and Start Uvicorn Server for REST API
    logger.info("Starting REST API Web Server...")
    port = int(os.environ.get("PORT", 8000))
    config = uvicorn.Config(app=web_app, host="0.0.0.0", port=port, log_level="info")
    server = uvicorn.Server(config)
    
    try:
        await server.serve()
    finally:
        logger.info("Shutting down Telegram Bot...")
        if bot_app.updater and bot_app.updater.running:
            await bot_app.updater.stop()
        await bot_app.stop()
        await bot_app.shutdown()
        logger.info("Shutdown completed.")

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
