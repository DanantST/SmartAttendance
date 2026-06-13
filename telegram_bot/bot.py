import logging
import asyncio
import datetime
import time
import os
import socket

# Force IPv4 DNS resolution globally to prevent Hugging Face IPv6 connection timeouts
orig_getaddrinfo = socket.getaddrinfo
def getaddrinfo_ipv4(host, port, family=0, type=0, proto=0, flags=0):
    return orig_getaddrinfo(host, port, socket.AF_INET, type, proto, flags)
socket.getaddrinfo = getaddrinfo_ipv4
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
import uvicorn
from telegram import (
    Update,
    ReplyKeyboardMarkup,
    KeyboardButton,
    ReplyKeyboardRemove,
    InlineKeyboardButton,
    InlineKeyboardMarkup,
)
from telegram.ext import (
    Application,
    ApplicationBuilder,
    CommandHandler,
    MessageHandler,
    ConversationHandler,
    CallbackQueryHandler,
    ContextTypes,
    filters,
)
import calendar
import io
import db

try:
    from dotenv import load_dotenv
    load_dotenv()
except ImportError:
    pass

# Setup Logging
logging.basicConfig(
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s", level=logging.INFO
)
logger = logging.getLogger(__name__)

# Telegram Bot Token
BOT_TOKEN = os.environ.get("TELEGRAM_BOT_TOKEN", "")

# Conversation States for Scheduling
(
    SELECT_COURSE,
    INPUT_COURSE_CODE,
    INPUT_COURSE_TITLE,
    SELECT_DATE,
    INPUT_DATE,
    SELECT_START_TIME,
    INPUT_START_TIME,
    SELECT_END_TIME,
    INPUT_END_TIME,
) = range(9)

# Conversation States for Course Enrollment
SELECT_ENROLL_COURSE = 9

# Conversation States for Unenrollment
UNENROLL_STUDENT_ID, UNENROLL_CONFIRM = range(10, 12)

# Conversation States for Report Selection
SELECT_REPORT_COURSE = 12

# FastAPI Application
web_app = FastAPI()

# Global Bot Application Reference
bot_app = None

# ----------------- FastAPI REST Endpoints -----------------

@web_app.get("/")
async def root_health():
    return {"status": "ok", "message": "Smart Attendance Bot is running"}

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
        
        # Fetch all users who have linked their Telegram account
        import sqlite3
        conn = sqlite3.connect(db.DB_PATH)
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()
        cursor.execute("SELECT uuid, telegram_id FROM users WHERE telegram_id IS NOT NULL AND telegram_id != ''")
        rows = cursor.fetchall()
        conn.close()
        
        mappings = [{"uuid": r["uuid"], "telegram_id": r["telegram_id"]} for r in rows]
        
        return JSONResponse(status_code=200, content={
            "status": "success", 
            "count": len(users),
            "users": mappings
        })
    except Exception as e:
        logger.error(f"Error syncing users: {e}")
        return JSONResponse(status_code=400, content={"status": "error", "message": str(e)})


@web_app.get("/api/dump_db")
async def dump_db():
    import sqlite3
    try:
        conn = sqlite3.connect(db.DB_PATH)
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()
        cursor.execute("SELECT * FROM users")
        users = [dict(r) for r in cursor.fetchall()]
        cursor.execute("SELECT * FROM schedules")
        schedules = [dict(r) for r in cursor.fetchall()]
        conn.close()
        return {"users": users, "schedules": schedules}
    except Exception as e:
        return {"status": "error", "message": str(e)}


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


@web_app.get("/api/get_report_requests")
async def get_report_requests():
    """
    Returns a list of pending report requests from the bot database.
    """
    try:
        requests = db.get_pending_report_requests()
        logger.info(f"Returning {len(requests)} pending report requests to device.")
        return JSONResponse(status_code=200, content=requests)
    except Exception as e:
        logger.error(f"Error fetching report requests: {e}")
        return JSONResponse(status_code=500, content={"status": "error", "message": str(e)})


@web_app.post("/api/upload_report")
async def upload_report(request: Request):
    """
    Receives JSON payload with CSV data and forwards it to the lecturer's Telegram chat.
    """
    try:
        payload = await request.json()
        request_id = payload.get("request_id")
        course_code = payload.get("course_code")
        lecturer_telegram_id = payload.get("lecturer_telegram_id")
        csv_data = payload.get("csv_data", "")
        
        logger.info(f"Received report for course {course_code} (request_id={request_id}) to forward to {lecturer_telegram_id}.")
        
        if lecturer_telegram_id:
            file_obj = io.BytesIO(csv_data.encode('utf-8'))
            file_obj.name = f"attendance_report_{course_code}.csv"
            
            global bot_app
            if bot_app:
                await bot_app.bot.send_document(
                    chat_id=lecturer_telegram_id,
                    document=file_obj,
                    caption=f"📊 Here is your requested attendance report for course **{course_code}**.",
                    parse_mode="Markdown"
                )
                logger.info(f"Report forwarded successfully to {lecturer_telegram_id}")
                if request_id:
                    db.complete_report_request(request_id)
                return {"status": "success", "message": "Report forwarded and request completed"}
            else:
                logger.error("bot_app is not initialized; cannot forward report")
                return JSONResponse(status_code=500, content={"status": "error", "message": "Bot not initialized"})
        
        return JSONResponse(status_code=400, content={"status": "error", "message": "Missing lecturer_telegram_id"})
    except Exception as e:
        logger.error(f"Error handling report upload: {e}")
        return JSONResponse(status_code=400, content={"status": "error", "message": str(e)})


@web_app.get("/api/link_user_dev")
async def link_user_dev(uuid: str, telegram_id: str):
    """
    Developer override endpoint to pair a user with a Telegram ID manually.
    """
    try:
        db.link_telegram_id(uuid, telegram_id)
        logger.info(f"Developer: Linked UUID {uuid} to Telegram ID {telegram_id}")
        return {"status": "success", "message": f"Linked UUID {uuid} to Telegram ID {telegram_id}"}
    except Exception as e:
        logger.error(f"Error linking dev user: {e}")
        return {"status": "error", "message": str(e)}


# ----------------- Telegram Bot Handlers -----------------

# ----------------- Telegram Bot Handlers -----------------

def build_calendar_keyboard(year: int, month: int):
    """
    Builds an inline keyboard showing a calendar month.
    """
    keyboard = []
    
    # Month and Year header
    month_name = calendar.month_name[month]
    keyboard.append([
        InlineKeyboardButton(f"{month_name} {year}", callback_data="cal_ignore")
    ])
    
    # Weekday headers
    weekdays = ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"]
    keyboard.append([
        InlineKeyboardButton(w, callback_data="cal_ignore") for w in weekdays
    ])
    
    # Days of the month
    month_calendar = calendar.monthcalendar(year, month)
    for week in month_calendar:
        row = []
        for day in week:
            if day == 0:
                row.append(InlineKeyboardButton(" ", callback_data="cal_ignore"))
            else:
                # Callback data: d:YYYYMMDD
                cb_data = f"d:{year}{month:02d}{day:02d}"
                row.append(InlineKeyboardButton(str(day), callback_data=cb_data))
        keyboard.append(row)
        
    # Navigation row
    prev_month = month - 1
    prev_year = year
    if prev_month == 0:
        prev_month = 12
        prev_year -= 1
        
    next_month = month + 1
    next_year = year
    if next_month == 13:
        next_month = 1
        next_year += 1
        
    keyboard.append([
        InlineKeyboardButton("◀️", callback_data=f"cal_nav:{prev_year}:{prev_month}"),
        InlineKeyboardButton("📅 Custom Date", callback_data="d:custom"),
        InlineKeyboardButton("▶️", callback_data=f"cal_nav:{next_year}:{next_month}")
    ])
    
    # Cancel row
    keyboard.append([
        InlineKeyboardButton("❌ Cancel", callback_data="c:cancel")
    ])
    
    return InlineKeyboardMarkup(keyboard)

def build_time_selector_keyboard(hour: int, minute: int, is_end_time: bool):
    """
    Builds an inline keyboard to adjust start or end times using increment/decrement.
    """
    keyboard = []
    label = "End Time" if is_end_time else "Start Time"
    time_str = f"{hour:02d}:{minute:02d}"
    
    keyboard.append([
        InlineKeyboardButton(f"⏰ Select {label}: {time_str}", callback_data="time_ignore")
    ])
    
    # Hour adjustment row
    keyboard.append([
        InlineKeyboardButton("-1h", callback_data=f"t_adj:H:-1:{int(is_end_time)}"),
        InlineKeyboardButton(f"{hour:02d} hr", callback_data="time_ignore"),
        InlineKeyboardButton("+1h", callback_data=f"t_adj:H:1:{int(is_end_time)}")
    ])
    
    # Minute adjustment row
    keyboard.append([
        InlineKeyboardButton("-10m", callback_data=f"t_adj:M:-10:{int(is_end_time)}"),
        InlineKeyboardButton("-5m", callback_data=f"t_adj:M:-5:{int(is_end_time)}"),
        InlineKeyboardButton(f"{minute:02d} min", callback_data="time_ignore"),
        InlineKeyboardButton("+5m", callback_data=f"t_adj:M:5:{int(is_end_time)}"),
        InlineKeyboardButton("+10m", callback_data=f"t_adj:M:10:{int(is_end_time)}")
    ])
    
    # Confirm / Custom / Cancel rows
    keyboard.append([
        InlineKeyboardButton("✍️ Enter Custom Time", callback_data="t:custom")
    ])
    keyboard.append([
        InlineKeyboardButton("❌ Cancel", callback_data="c:cancel"),
        InlineKeyboardButton("🆗 Confirm", callback_data=f"t_conf:{hour}:{minute}:{int(is_end_time)}")
    ])
    
    return InlineKeyboardMarkup(keyboard)

async def start_cmd(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Greets the user and checks registration status. Displays role-based keyboards.
    """
    user_id = str(update.effective_user.id)
    user = db.get_user_by_telegram_id(user_id)
    
    if user:
        role = user.get("role")
        name = user.get("name")
        if role in ["lecturer", "admin"]:
            reply_keyboard = ReplyKeyboardMarkup(
                [
                    ["📅 Schedule Class", "📊 Attendance Report"],
                    ["📚 My Courses", "🛠️ Developer Auth"],
                    ["❓ Help"]
                ],
                resize_keyboard=True
            )
            await update.message.reply_text(
                f"Welcome back, {name}! You are registered as a **{role.capitalize()}**.\n\n"
                "Please select an option from the menu below:",
                reply_markup=reply_keyboard
            )
        else:
            reply_keyboard = ReplyKeyboardMarkup(
                [
                    ["📚 Enroll in Course"],
                    ["👤 My Status", "❓ Help"]
                ],
                resize_keyboard=True
            )
            await update.message.reply_text(
                f"Hello {name}, you are registered as a **Student**.\n"
                "You will receive automatic notifications when your attendance is recorded.\n\n"
                "📚 Use the menu below to enroll in courses.",
                reply_markup=reply_keyboard
            )
    else:
        # Prompt to share phone number to link accounts
        contact_keyboard = ReplyKeyboardMarkup(
            [[KeyboardButton(text="📱 Share Contact Details", request_contact=True)]],
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
        
        if role in ["lecturer", "admin"]:
            reply_keyboard = ReplyKeyboardMarkup(
                [
                    ["📅 Schedule Class", "📊 Attendance Report"],
                    ["📚 My Courses", "🛠️ Developer Auth"],
                    ["❓ Help"]
                ],
                resize_keyboard=True
            )
        else:
            reply_keyboard = ReplyKeyboardMarkup(
                [
                    ["📚 Enroll in Course"],
                    ["👤 My Status", "❓ Help"]
                ],
                resize_keyboard=True
            )
            
        await update.message.reply_text(
            f"Registration successful! ✅\n\n"
            f"**Name:** {name}\n"
            f"**Role:** {role.capitalize()}\n\n"
            "Your Telegram account has been linked to the attendance device.",
            reply_markup=reply_keyboard,
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
    
    reply_keyboard = ReplyKeyboardMarkup(
        [
            ["📅 Schedule Class", "📊 Attendance Report"],
            ["📚 My Courses", "🛠️ Developer Auth"],
            ["❓ Help"]
        ],
        resize_keyboard=True
    )
    
    await update.message.reply_text(
        "🛠️ Developer Override: You have been registered in the local DB as a **Lecturer**.\n"
        "You can now schedule test classes and request reports.",
        reply_markup=reply_keyboard
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

async def help_cmd(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Prints bot help text."""
    await update.message.reply_text(
        "📚 **Smart Attendance System Bot Help** 📚\n\n"
        "This bot is integrated with the CrowPanel ESP32-P4 device to manage attendance.\n\n"
        "**Available Actions:**\n"
        "• Tap any of the menu buttons below to interact.\n"
        "• **Schedule Class:** (Lecturers) Creates a new lecture slot with calendar and time dials.\n"
        "• **Attendance Report:** (Lecturers) Requests a CSV report of a course. The admin is notified to sync, and it's sent to you.\n"
        "• **Enroll in Course:** (Students) Registers you to courses using easy button selections.\n"
        "• **My Status:** Check your local registration card details.\n"
        "• **My Courses:** Lists all courses assigned to you.\n"
        "• **Cancel:** Use `/cancel` or click Cancel to exit any setup wizard."
    )

async def my_courses_cmd(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Lists courses assigned to the lecturer."""
    user_id = str(update.effective_user.id)
    user = db.get_user_by_telegram_id(user_id)
    if not user or user.get("role") not in ["lecturer", "admin"]:
        await update.message.reply_text("❌ Unauthorized. Only registered Lecturers or Admins can view courses.")
        return
    
    courses = db.get_lecturer_courses(user_id)
    if courses:
        course_list = "\n".join(f"  • **{c['course_code']}** — {c['name']}" for c in courses)
        await update.message.reply_text(f"📚 **Your Assigned Courses:**\n\n{course_list}", parse_mode="Markdown")
    else:
        await update.message.reply_text("📚 You have no courses assigned yet. Tap **📅 Schedule Class** and add a course to assign it to yourself.")

async def cancel_cmd(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Aborts any active conversation.
    """
    await update.message.reply_text(
        "Operation cancelled.", reply_markup=ReplyKeyboardRemove()
    )
    # Re-display keyboard if registered
    user_id = str(update.effective_user.id)
    user = db.get_user_by_telegram_id(user_id)
    if user:
        role = user.get("role")
        if role in ["lecturer", "admin"]:
            reply_keyboard = ReplyKeyboardMarkup(
                [
                    ["📅 Schedule Class", "📊 Attendance Report"],
                    ["📚 My Courses", "🛠️ Developer Auth"],
                    ["❓ Help"]
                ],
                resize_keyboard=True
            )
        else:
            reply_keyboard = ReplyKeyboardMarkup(
                [
                    ["📚 Enroll in Course"],
                    ["👤 My Status", "❓ Help"]
                ],
                resize_keyboard=True
            )
        await update.message.reply_text("Returned to main menu:", reply_markup=reply_keyboard)
        
    return ConversationHandler.END

# ----------------- Scheduling Conversation Flow -----------------

async def schedule_start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Entry point for scheduling. Verifies user is authorized.
    """
    user_id = str(update.effective_user.id)
    user = db.get_user_by_telegram_id(user_id)
    
    if not user or user.get("role") not in ["lecturer", "admin"]:
        msg = update.message if update.message else update.callback_query.message
        await msg.reply_text("❌ Unauthorized. Only registered Lecturers or Admins can schedule lectures.")
        return ConversationHandler.END

    courses = db.get_lecturer_courses(user_id)
    if not courses:
        # If no courses are assigned, go straight to manual input
        await update.message.reply_text(
            "You have no courses assigned yet.\n"
            "Please enter the **Course Code** (e.g., CSC301) to begin:"
        )
        return INPUT_COURSE_CODE

    # Present list of assigned courses as inline buttons
    keyboard = []
    for c in courses:
        keyboard.append([InlineKeyboardButton(f"{c['course_code']} - {c['name']}", callback_data=f"c:{c['course_code']}")])
    keyboard.append([InlineKeyboardButton("➕ New Course (Create)", callback_data="c:new")])
    keyboard.append([InlineKeyboardButton("❌ Cancel", callback_data="c:cancel")])
    
    reply_markup = InlineKeyboardMarkup(keyboard)
    await update.message.reply_text(
        "Let's schedule a lecture.\n"
        "Please select a course from your assigned list or create a new one:",
        reply_markup=reply_markup
    )
    return SELECT_COURSE

async def schedule_select_course(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()
    data = query.data
    
    if data == "c:cancel":
        await query.edit_message_text("Operation cancelled.")
        return ConversationHandler.END
        
    if data == "c:new":
        await query.edit_message_text(
            "Please enter the **Course Code** (e.g., CSC301) for the new course:"
        )
        return INPUT_COURSE_CODE
        
    course_code = data.split(":")[1]
    context.user_data["course_code"] = course_code
    
    # Fetch course title from database
    courses = db.get_all_courses()
    course_title = "Scheduled Course"
    for c in courses:
        if c["code"] == course_code:
            course_title = c["name"]
            break
    context.user_data["course_title"] = course_title
    
    # Direct to date selection via calendar UI
    now = datetime.datetime.now()
    reply_markup = build_calendar_keyboard(now.year, now.month)
    await query.edit_message_text(
        f"Selected Course: **{course_code} - {course_title}**\n\n"
        "Please select the **Date** for the lecture from the calendar:",
        reply_markup=reply_markup,
        parse_mode="Markdown"
    )
    return SELECT_DATE

async def schedule_input_course_code(update: Update, context: ContextTypes.DEFAULT_TYPE):
    course_code = update.message.text.strip().upper()
    if not course_code:
        await update.message.reply_text("Please enter a valid course code:")
        return INPUT_COURSE_CODE
        
    context.user_data["course_code"] = course_code
    await update.message.reply_text(
        f"Course Code set to: {course_code}\n\n"
        "Now, please enter the **Course Title** (e.g., Database Systems):"
    )
    return INPUT_COURSE_TITLE

async def schedule_input_course_title(update: Update, context: ContextTypes.DEFAULT_TYPE):
    course_title = update.message.text.strip()
    if not course_title:
        await update.message.reply_text("Please enter a valid course title:")
        return INPUT_COURSE_TITLE
        
    context.user_data["course_title"] = course_title
    
    # Auto-link this new course to the lecturer
    user_id = str(update.effective_user.id)
    db.link_lecturer_course(user_id, context.user_data["course_code"])
    
    # Direct to date selection
    now = datetime.datetime.now()
    reply_markup = build_calendar_keyboard(now.year, now.month)
    await update.message.reply_text(
        f"New Course Registered: **{context.user_data['course_code']} - {course_title}**\n\n"
        "Please select the **Date** for the lecture from the calendar:",
        reply_markup=reply_markup,
        parse_mode="Markdown"
    )
    return SELECT_DATE

async def schedule_select_date_nav(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()
    data = query.data
    
    parts = data.split(":")
    year = int(parts[1])
    month = int(parts[2])
    
    reply_markup = build_calendar_keyboard(year, month)
    course_code = context.user_data.get("course_code", "Course")
    course_title = context.user_data.get("course_title", "")
    await query.edit_message_text(
        f"Selected Course: **{course_code} - {course_title}**\n\n"
        "Please select the **Date** for the lecture from the calendar:",
        reply_markup=reply_markup,
        parse_mode="Markdown"
    )
    return SELECT_DATE

async def schedule_select_date_val(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()
    data = query.data
    
    val = data.split(":")[1]
    year = int(val[0:4])
    month = int(val[4:6])
    day = int(val[6:8])
    
    date_str = f"{day:02d}/{month:02d}/{year}"
    context.user_data["date"] = date_str
    
    reply_markup = build_time_selector_keyboard(9, 0, is_end_time=False)
    await query.edit_message_text(
        f"Selected Course: **{context.user_data.get('course_code')}**\n"
        f"Selected Date: **{date_str}**\n\n"
        "Please select/adjust the **Start Time** for the lecture:",
        reply_markup=reply_markup,
        parse_mode="Markdown"
    )
    return SELECT_START_TIME

async def schedule_select_date_custom(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()
    await query.edit_message_text(
        "Please enter the **Date** of the lecture (Format: DD/MM/YYYY):"
    )
    return INPUT_DATE

async def schedule_input_date(update: Update, context: ContextTypes.DEFAULT_TYPE):
    date_str = update.message.text.strip()
    try:
        datetime.datetime.strptime(date_str, "%d/%m/%Y")
    except ValueError:
        await update.message.reply_text(
            "❌ Invalid date format. Please write it exactly as DD/MM/YYYY (e.g., 18/06/2026):"
        )
        return INPUT_DATE

    context.user_data["date"] = date_str
    reply_markup = build_time_selector_keyboard(9, 0, is_end_time=False)
    await update.message.reply_text(
        f"Selected Date: **{date_str}**\n\n"
        "Please select/adjust the **Start Time** for the lecture:",
        reply_markup=reply_markup,
        parse_mode="Markdown"
    )
    return SELECT_START_TIME

async def schedule_time_adj(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()
    data = query.data
    
    parts = data.split(":")
    field = parts[1]
    delta = int(parts[2])
    is_end_time = bool(int(parts[3]))
    
    current_label_btn = query.message.reply_markup.inline_keyboard[0][0].text
    time_part = current_label_btn.split(": ")[1]
    hour, minute = map(int, time_part.split(":"))
    
    if field == "H":
        hour = (hour + delta) % 24
    elif field == "M":
        minute = (minute + delta) % 60
        
    reply_markup = build_time_selector_keyboard(hour, minute, is_end_time)
    label = "End Time" if is_end_time else "Start Time"
    course = context.user_data.get("course_code")
    date = context.user_data.get("date")
    
    await query.edit_message_text(
        f"Selected Course: **{course}**\n"
        f"Selected Date: **{date}**\n\n"
        f"Please select/adjust the **{label}** for the lecture:",
        reply_markup=reply_markup,
        parse_mode="Markdown"
    )
    return SELECT_END_TIME if is_end_time else SELECT_START_TIME

async def schedule_time_custom(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()
    
    state = context.user_data.get("time_state", "start")
    if state == "end":
        await query.edit_message_text(
            "Please enter the **End Time** (Format: HH:MM, 24-hour clock, e.g. 11:00):"
        )
        return INPUT_END_TIME
    else:
        await query.edit_message_text(
            "Please enter the **Start Time** (Format: HH:MM, 24-hour clock, e.g. 09:00):"
        )
        return INPUT_START_TIME

async def schedule_input_start_time(update: Update, context: ContextTypes.DEFAULT_TYPE):
    time_str = update.message.text.strip()
    try:
        datetime.datetime.strptime(time_str, "%H:%M")
    except ValueError:
        await update.message.reply_text(
            "❌ Invalid time format. Please write it exactly as HH:MM (e.g., 09:30 or 14:00):"
        )
        return INPUT_START_TIME

    context.user_data["start_time"] = time_str
    context.user_data["time_state"] = "end"
    
    h, m = map(int, time_str.split(":"))
    end_h = (h + 2) % 24
    reply_markup = build_time_selector_keyboard(end_h, m, is_end_time=True)
    await update.message.reply_text(
        f"Start Time set to: **{time_str}**\n\n"
        "Please select/adjust the **End Time** for the lecture:",
        reply_markup=reply_markup,
        parse_mode="Markdown"
    )
    return SELECT_END_TIME

async def schedule_time_confirm(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()
    data = query.data
    
    parts = data.split(":")
    hour = int(parts[1])
    minute = int(parts[2])
    is_end_time = bool(int(parts[3]))
    
    time_str = f"{hour:02d}:{minute:02d}"
    
    if not is_end_time:
        context.user_data["start_time"] = time_str
        context.user_data["time_state"] = "end"
        
        end_h = (hour + 2) % 24
        reply_markup = build_time_selector_keyboard(end_h, minute, is_end_time=True)
        await query.edit_message_text(
            f"Selected Course: **{context.user_data.get('course_code')}**\n"
            f"Selected Date: **{context.user_data.get('date')}**\n"
            f"Start Time set to: **{time_str}**\n\n"
            "Please select/adjust the **End Time** for the lecture:",
            reply_markup=reply_markup,
            parse_mode="Markdown"
        )
        return SELECT_END_TIME
    else:
        context.user_data["end_time"] = time_str
        return await complete_schedule_creation(query.message, context, is_callback=True)

async def schedule_input_end_time(update: Update, context: ContextTypes.DEFAULT_TYPE):
    time_str = update.message.text.strip()
    try:
        datetime.datetime.strptime(time_str, "%H:%M")
    except ValueError:
        await update.message.reply_text(
            "❌ Invalid time format. Please write it exactly as HH:MM (e.g., 11:00 or 16:30):"
        )
        return INPUT_END_TIME

    context.user_data["end_time"] = time_str
    return await complete_schedule_creation(update.message, context, is_callback=False)

async def complete_schedule_creation(message, context, is_callback):
    date_str = context.user_data["date"]
    start_time_str = context.user_data["start_time"]
    end_time_str = context.user_data["end_time"]
    
    try:
        start_dt = datetime.datetime.strptime(f"{date_str} {start_time_str}", "%d/%m/%Y %H:%M")
        end_dt = datetime.datetime.strptime(f"{date_str} {end_time_str}", "%d/%m/%Y %H:%M")
        
        start_epoch = int(start_dt.timestamp())
        end_epoch = int(end_dt.timestamp())
        
        if end_epoch <= start_epoch:
            err_msg = "❌ End time must be strictly after the start time. Please select/input the End Time again:"
            if is_callback:
                h, m = map(int, end_time_str.split(":"))
                reply_markup = build_time_selector_keyboard(h, m, is_end_time=True)
                await message.edit_text(err_msg, reply_markup=reply_markup)
                return SELECT_END_TIME
            else:
                await message.reply_text(err_msg)
                return INPUT_END_TIME

        user_id = str(context._user_id)
        user = db.get_user_by_telegram_id(user_id)
        lecturer_name = user.get("name") if user else "Lecturer"
        
        course_code = context.user_data["course_code"]
        course_title = context.user_data["course_title"]
        
        db.add_schedule(user_id, course_code, course_title, start_epoch, end_epoch)
        
        success_text = (
            "✅ **Lecture Scheduled Successfully!**\n\n"
            f"**Course:** {course_code} - {course_title}\n"
            f"**Date:** {date_str}\n"
            f"**Time:** {start_time_str} - {end_time_str}\n\n"
            "The device will fetch and sync this lecture automatically during its next cloud sync cycle."
        )
        
        # Display the lecturer keyboard again
        reply_keyboard = ReplyKeyboardMarkup(
            [
                ["📅 Schedule Class", "📊 Attendance Report"],
                ["📚 My Courses", "🛠️ Developer Auth"],
                ["❓ Help"]
            ],
            resize_keyboard=True
        )
        
        if is_callback:
            # We can't set a reply_markup on edit_message_text, so we send a separate message for the success text
            # and clean up the original inline prompt
            await message.edit_text(f"Creating schedule for {course_code}...")
            await message.reply_text(success_text, reply_markup=reply_keyboard, parse_mode="Markdown")
        else:
            await message.reply_text(success_text, reply_markup=reply_keyboard, parse_mode="Markdown")
            
        return ConversationHandler.END

    except Exception as e:
        logger.error(f"Error parsing date/times for schedule: {e}")
        err_msg = "❌ An internal error occurred. Please try again with /schedule."
        if is_callback:
            await message.edit_text(err_msg)
        else:
            await message.reply_text(err_msg)
        return ConversationHandler.END

async def schedule_cancel_callback(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()
    await query.edit_message_text("Operation cancelled.")
    
    user_id = str(context._user_id)
    user = db.get_user_by_telegram_id(user_id)
    if user:
        role = user.get("role")
        if role in ["lecturer", "admin"]:
            reply_keyboard = ReplyKeyboardMarkup(
                [
                    ["📅 Schedule Class", "📊 Attendance Report"],
                    ["📚 My Courses", "🛠️ Developer Auth"],
                    ["❓ Help"]
                ],
                resize_keyboard=True
            )
        else:
            reply_keyboard = ReplyKeyboardMarkup(
                [
                    ["📚 Enroll in Course"],
                    ["👤 My Status", "❓ Help"]
                ],
                resize_keyboard=True
            )
        await query.message.reply_text("Returned to main menu:", reply_markup=reply_keyboard)
    return ConversationHandler.END

# ----------------- Course Enrollment Conversation (Students) -----------------

async def enroll_course_start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Entry point: verify user is a registered student, then show available courses as inline buttons.
    """
    user_id = str(update.effective_user.id)
    user = db.get_user_by_telegram_id(user_id)

    if not user:
        await update.message.reply_text(
            "You are not registered yet. Please use /start and share your contact first."
        )
        return ConversationHandler.END

    courses = db.get_all_courses()
    if not courses:
        await update.message.reply_text("No courses are currently registered in the database.")
        return ConversationHandler.END

    # Present list of available courses as inline buttons
    keyboard = []
    for c in courses:
        keyboard.append([InlineKeyboardButton(f"{c['code']} - {c['name']}", callback_data=f"e:{c['code']}")])
    keyboard.append([InlineKeyboardButton("❌ Cancel", callback_data="e:cancel")])
    
    reply_markup = InlineKeyboardMarkup(keyboard)
    await update.message.reply_text(
        "Please select a course you want to enroll in:",
        reply_markup=reply_markup
    )
    return SELECT_ENROLL_COURSE

async def enroll_course_callback(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()
    data = query.data
    
    if data == "e:cancel":
        await query.edit_message_text("Operation cancelled.")
        return ConversationHandler.END
        
    code = data.split(":")[1]
    user_id = str(context._user_id)
    user = db.get_user_by_telegram_id(user_id)
    
    if not user:
        await query.edit_message_text("Session expired. Please use /start again.")
        return ConversationHandler.END

    reply_keyboard = ReplyKeyboardMarkup(
        [
            ["📚 Enroll in Course"],
            ["👤 My Status", "❓ Help"]
        ],
        resize_keyboard=True
    )

    result = db.enroll_user_in_course(user["uuid"], code)
    if result == "already_enrolled":
        await query.edit_message_text(f"ℹ️ You are already enrolled in **{code}**.", parse_mode="Markdown")
        await query.message.reply_text("Returned to main menu:", reply_markup=reply_keyboard)
    elif result == "ok":
        await query.edit_message_text(f"Enrolling in {code}...")
        await query.message.reply_text(
            f"✅ You have been successfully enrolled in **{code}**.\n"
            "The attendance device will sync this enrollment during its next cloud sync cycle.",
            reply_markup=reply_keyboard,
            parse_mode="Markdown"
        )
    else:
        await query.edit_message_text("❌ An error occurred. Please try again with /enroll_course.")
        await query.message.reply_text("Returned to main menu:", reply_markup=reply_keyboard)

    return ConversationHandler.END

# ----------------- Attendance Report Conversation (Lecturers) -----------------

async def report_start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """
    Entry point for reports. Displays courses assigned to the lecturer.
    """
    user_id = str(update.effective_user.id)
    user = db.get_user_by_telegram_id(user_id)
    
    if not user or user.get("role") not in ["lecturer", "admin"]:
        msg = update.message if update.message else update.callback_query.message
        await msg.reply_text("❌ Unauthorized. Only registered Lecturers or Admins can request attendance reports.")
        return ConversationHandler.END

    courses = db.get_lecturer_courses(user_id)
    if not courses:
        await update.message.reply_text("📚 You have no courses assigned. Reports can only be generated for your courses.")
        return ConversationHandler.END

    keyboard = []
    for c in courses:
        keyboard.append([InlineKeyboardButton(f"{c['course_code']} - {c['name']}", callback_data=f"rep:{c['course_code']}")])
    keyboard.append([InlineKeyboardButton("❌ Cancel", callback_data="rep:cancel")])
    
    reply_markup = InlineKeyboardMarkup(keyboard)
    await update.message.reply_text(
        "Please select the course you want the attendance report for:",
        reply_markup=reply_markup
    )
    return SELECT_REPORT_COURSE

async def report_select_course(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()
    data = query.data
    
    if data == "rep:cancel":
        await query.edit_message_text("Operation cancelled.")
        return ConversationHandler.END
        
    course_code = data.split(":")[1]
    user_id = str(context._user_id)
    user = db.get_user_by_telegram_id(user_id)
    
    if not user:
        await query.edit_message_text("Session expired. Please use /start again.")
        return ConversationHandler.END
        
    lecturer_name = user.get("name", "Lecturer")
    db.add_report_request(course_code, user_id)
    
    # Notify Admin if registered
    admin_id = db.get_admin_telegram_id()
    admin_notified = False
    if admin_id:
        try:
            await context.bot.send_message(
                chat_id=admin_id,
                text=(
                    f"🔔 **Admin Notification:**\n"
                    f"Lecturer **{lecturer_name}** has requested an attendance report for course **{course_code}**.\n\n"
                    f"Please turn on or connect the attendance device to the internet to complete the sync and deliver the report."
                ),
                parse_mode="Markdown"
            )
            admin_notified = True
        except Exception as e:
            logger.error(f"Failed to send message to admin {admin_id}: {e}")
            
    response_text = (
        f"📥 **Attendance Report Requested for {course_code}**\n\n"
        f"Since the device operates offline to conserve power, a notification "
        f"has been sent to the Admin to connect the device to the internet.\n\n"
        f"Your CSV report will be compiled and delivered to you here automatically on the next device sync."
    )
    if not admin_notified:
        response_text += "\n\n*(Note: No admin was notified as no active admin Telegram ID was found in the database. Please prompt the administrator to connect the device manually.)*"
        
    reply_keyboard = ReplyKeyboardMarkup(
        [
            ["📅 Schedule Class", "📊 Attendance Report"],
            ["📚 My Courses", "🛠️ Developer Auth"],
            ["❓ Help"]
        ],
        resize_keyboard=True
    )
    
    await query.edit_message_text(f"Requesting report for {course_code}...")
    await query.message.reply_text(response_text, reply_markup=reply_keyboard, parse_mode="Markdown")
    return ConversationHandler.END

# ----------------- Unenrollment Conversation Handler (Lecturers/Admins) -----------------

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

    reply_keyboard = ReplyKeyboardMarkup(
        [
            ["📅 Schedule Class", "📊 Attendance Report"],
            ["📚 My Courses", "🛠️ Developer Auth"],
            ["❓ Help"]
        ],
        resize_keyboard=True
    )

    if confirm == "YES":
        if db.delete_user_by_uuid(uuid):
            await update.message.reply_text(
                f"✅ **Student Successfully Unenrolled!**\n\n"
                f"**Name:** {name}\n"
                f"**Student ID:** {student_id}\n\n"
                f"The attendance device will delete this user from its local database during its next sync cycle.",
                reply_markup=reply_keyboard
            )
        else:
            await update.message.reply_text("❌ An error occurred while deleting the student. Please try again.", reply_markup=reply_keyboard)
        return ConversationHandler.END
    elif confirm == "NO":
        await update.message.reply_text("Operation cancelled.", reply_markup=reply_keyboard)
        return ConversationHandler.END
    else:
        await update.message.reply_text("Please type either **YES** or **NO**:")
        return UNENROLL_CONFIRM

async def button_mapper(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Maps custom Reply Keyboard text clicks to command handlers."""
    text = update.message.text
    if text == "📅 Schedule Class":
        return await schedule_start(update, context)
    elif text == "📊 Attendance Report":
        return await report_start(update, context)
    elif text == "📚 My Courses":
        await my_courses_cmd(update, context)
    elif text == "🛠️ Developer Auth":
        await auth_me_dev_cmd(update, context)
    elif text == "❓ Help":
        await help_cmd(update, context)
    elif text == "📚 Enroll in Course":
        return await enroll_course_start(update, context)
    elif text == "👤 My Status":
        await status_cmd(update, context)

# ----------------- Main Runner -----------------

async def main():
    global bot_app
    # 1. Initialize SQLite Database
    db.init_db()
    
    if not BOT_TOKEN:
        logger.error("TELEGRAM_BOT_TOKEN environment variable is not set! Please set it and try again.")
        return
        
    # 2. Build Telegram Bot Application
    api_url = os.environ.get("TELEGRAM_API_URL", "https://api.telegram.org/bot")
    logger.info(f"Using Telegram API endpoint: {api_url}")
    bot_app = ApplicationBuilder().token(BOT_TOKEN).base_url(api_url).build()
    
    # Registration & Commands
    bot_app.add_handler(CommandHandler("start", start_cmd))
    bot_app.add_handler(CommandHandler("auth_me_dev", auth_me_dev_cmd))
    bot_app.add_handler(CommandHandler("status", status_cmd))
    bot_app.add_handler(CommandHandler("report", report_start))
    bot_app.add_handler(CommandHandler("courses", my_courses_cmd))
    bot_app.add_handler(CommandHandler("help", help_cmd))
    bot_app.add_handler(MessageHandler(filters.CONTACT, contact_handler))




    # Course Enrollment Conversation (Students)
    enroll_conv = ConversationHandler(
        entry_points=[
            CommandHandler("enroll_course", enroll_course_start),
            MessageHandler(filters.Regex(r"^📚 Enroll in Course$"), enroll_course_start)
        ],
        states={
            SELECT_ENROLL_COURSE: [
                CallbackQueryHandler(enroll_course_callback, pattern=r"^e:[^c]"),
                CallbackQueryHandler(schedule_cancel_callback, pattern=r"^e:cancel")
            ],
        },
        fallbacks=[CommandHandler("cancel", cancel_cmd)],
    )
    bot_app.add_handler(enroll_conv)

    # Attendance Report Request Conversation (Lecturers)
    report_conv = ConversationHandler(
        entry_points=[
            CommandHandler("report", report_start),
            MessageHandler(filters.Regex(r"^📊 Attendance Report$"), report_start)
        ],
        states={
            SELECT_REPORT_COURSE: [
                CallbackQueryHandler(report_select_course, pattern=r"^rep:[^c]"),
                CallbackQueryHandler(schedule_cancel_callback, pattern=r"^rep:cancel")
            ]
        },
        fallbacks=[CommandHandler("cancel", cancel_cmd)],
    )
    bot_app.add_handler(report_conv)

    # Scheduling Conversation Handler (Lecturers/Admins)
    schedule_conv = ConversationHandler(
        entry_points=[
            CommandHandler("schedule", schedule_start),
            MessageHandler(filters.Regex(r"^📅 Schedule Class$"), schedule_start)
        ],
        states={
            SELECT_COURSE: [
                CallbackQueryHandler(schedule_select_course, pattern=r"^c:")
            ],
            INPUT_COURSE_CODE: [
                MessageHandler(filters.TEXT & ~filters.COMMAND, schedule_input_course_code)
            ],
            INPUT_COURSE_TITLE: [
                MessageHandler(filters.TEXT & ~filters.COMMAND, schedule_input_course_title)
            ],
            SELECT_DATE: [
                CallbackQueryHandler(schedule_select_date_nav, pattern=r"^cal_nav:"),
                CallbackQueryHandler(schedule_select_date_val, pattern=r"^d:\d+"),
                CallbackQueryHandler(schedule_select_date_custom, pattern=r"^d:custom"),
                CallbackQueryHandler(schedule_cancel_callback, pattern=r"^c:cancel")
            ],
            INPUT_DATE: [
                MessageHandler(filters.TEXT & ~filters.COMMAND, schedule_input_date)
            ],
            SELECT_START_TIME: [
                CallbackQueryHandler(schedule_time_adj, pattern=r"^t_adj:"),
                CallbackQueryHandler(schedule_time_custom, pattern=r"^t:custom"),
                CallbackQueryHandler(schedule_time_confirm, pattern=r"^t_conf:"),
                CallbackQueryHandler(schedule_cancel_callback, pattern=r"^c:cancel")
            ],
            INPUT_START_TIME: [
                MessageHandler(filters.TEXT & ~filters.COMMAND, schedule_input_start_time)
            ],
            SELECT_END_TIME: [
                CallbackQueryHandler(schedule_time_adj, pattern=r"^t_adj:"),
                CallbackQueryHandler(schedule_time_custom, pattern=r"^t:custom"),
                CallbackQueryHandler(schedule_time_confirm, pattern=r"^t_conf:"),
                CallbackQueryHandler(schedule_cancel_callback, pattern=r"^c:cancel")
            ],
            INPUT_END_TIME: [
                MessageHandler(filters.TEXT & ~filters.COMMAND, schedule_input_end_time)
            ],
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
    
    # Reply Keyboard Button Mapper (general text mapper when not in conversation, registered last to avoid shadowing conversation entry points)
    button_filter = filters.TEXT & filters.Regex(r"^(📅 Schedule Class|📊 Attendance Report|📚 My Courses|🛠️ Developer Auth|❓ Help|📚 Enroll in Course|👤 My Status)$")
    bot_app.add_handler(MessageHandler(button_filter, button_mapper))
    
    # Initialize Bot App
    logger.info("Initializing Telegram Bot...")
    await bot_app.initialize()
    await bot_app.start()
    
    # Check if we should use Webhook (Render/Koyeb) or Polling (Local Dev)
    public_url = os.environ.get("PUBLIC_URL") or os.environ.get("RENDER_EXTERNAL_URL")
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
    
    # Check if we should start the web server (default is True)
    start_web_server = os.environ.get("START_WEB_SERVER", "true").lower() == "true"
    
    if start_web_server:
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
    else:
        # Just block forever while polling runs in the background
        logger.info("Web server disabled (START_WEB_SERVER=false). Running bot in polling mode only.")
        try:
            while True:
                await asyncio.sleep(3600)
        except asyncio.CancelledError:
            pass
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
