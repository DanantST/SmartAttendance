import sqlite3
import logging
import time
import os

logger = logging.getLogger(__name__)

DB_PATH = os.environ.get("DATABASE_PATH", os.path.join(os.path.dirname(__file__), "bot_data.db"))

def init_db():
    """Initializes the database schema."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # Table for users synced from the device
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS users (
        uuid TEXT PRIMARY KEY,
        name TEXT NOT NULL,
        student_id TEXT,
        phone_number TEXT,
        telegram_id TEXT,
        role TEXT NOT NULL,
        updated_at INTEGER NOT NULL
    )
    """)
    
    # Table for schedules created by lecturers
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS schedules (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        telegram_id TEXT NOT NULL,
        course_code TEXT NOT NULL,
        course_title TEXT NOT NULL,
        start_time INTEGER NOT NULL,
        end_time INTEGER NOT NULL,
        created_at INTEGER NOT NULL
    )
    """)
    
    # Table for courses
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS courses (
        code TEXT PRIMARY KEY,
        name TEXT NOT NULL
    )
    """)
    
    # Table for user course enrollments (students)
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS user_courses (
        user_uuid TEXT NOT NULL,
        course_code TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        PRIMARY KEY (user_uuid, course_code)
    )
    """)

    # Table to track remote user deletions
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS deletions (
        user_uuid TEXT PRIMARY KEY,
        deleted_at INTEGER NOT NULL
    )
    """)
    
    # Table for lecturer course assignments
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS lecturer_courses (
        lecturer_telegram_id TEXT,
        course_code TEXT,
        PRIMARY KEY (lecturer_telegram_id, course_code)
    )
    """)
    
    # Table for attendance report requests
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS report_requests (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        course_code TEXT NOT NULL,
        lecturer_telegram_id TEXT NOT NULL,
        status TEXT DEFAULT 'pending',
        created_at INTEGER NOT NULL
    )
    """)
    
    conn.commit()
    conn.close()
    logger.info("Database initialized successfully.")

def normalize_phone(phone):
    """Normalizes phone number by keeping only digits."""
    if not phone:
        return ""
    return "".join(c for c in phone if c.isdigit())

def find_user_by_phone(phone_number):
    """
    Finds a user by phone number using normalized suffix matching (last 9 digits).
    This handles differences in country codes (e.g. +234803... vs 0803... or 234803...).
    """
    normalized_target = normalize_phone(phone_number)
    if not normalized_target or len(normalized_target) < 9:
        return None
        
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("SELECT uuid, name, student_id, phone_number, telegram_id, role FROM users")
    rows = cursor.fetchall()
    conn.close()
    
    target_suffix = normalized_target[-9:]
    for row in rows:
        db_phone = normalize_phone(row["phone_number"])
        if db_phone and len(db_phone) >= 9 and db_phone[-9:] == target_suffix:
            return dict(row)
            
    return None

def upsert_user(uuid, name, student_id, phone_number, telegram_id, role):
    """Inserts or updates a user record."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # Check if user already exists
    cursor.execute("SELECT telegram_id, phone_number FROM users WHERE uuid = ?", (uuid,))
    row = cursor.fetchone()
    
    now = int(time.time())
    if row:
        # User exists, update fields but preserve telegram_id if it was set via bot / start
        existing_tel_id = row[0]
        final_tel_id = telegram_id if telegram_id else existing_tel_id
        cursor.execute("""
            UPDATE users 
            SET name = ?, student_id = ?, phone_number = ?, telegram_id = ?, role = ?, updated_at = ?
            WHERE uuid = ?
        """, (name, student_id, phone_number, final_tel_id, role, now, uuid))
    else:
        cursor.execute("""
            INSERT INTO users (uuid, name, student_id, phone_number, telegram_id, role, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        """, (uuid, name, student_id, phone_number, telegram_id, role, now))
        
    conn.commit()
    conn.close()

def link_telegram_id(uuid, telegram_id):
    """Links a telegram_id to a specific user UUID."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    now = int(time.time())
    cursor.execute("""
        UPDATE users 
        SET telegram_id = ?, updated_at = ?
        WHERE uuid = ?
    """, (telegram_id, now, uuid))
    conn.commit()
    conn.close()

def get_user_by_telegram_id(telegram_id):
    """Retrieves a user by their Telegram ID."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("""
        SELECT uuid, name, student_id, phone_number, telegram_id, role 
        FROM users 
        WHERE telegram_id = ?
    """, (str(telegram_id),))
    row = cursor.fetchone()
    conn.close()
    return dict(row) if row else None

def add_schedule(telegram_id, course_code, course_title, start_time, end_time):
    """Creates a new schedule record and populates/updates the courses table."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    now = int(time.time())
    
    # 1. Insert course schedule
    cursor.execute("""
        INSERT INTO schedules (telegram_id, course_code, course_title, start_time, end_time, created_at)
        VALUES (?, ?, ?, ?, ?, ?)
    """, (str(telegram_id), course_code, course_title, int(start_time), int(end_time), now))
    
    # 2. Insert or update the course details in courses table
    cursor.execute("""
        INSERT OR IGNORE INTO courses (code, name)
        VALUES (?, ?)
    """, (course_code, course_title))
    cursor.execute("""
        UPDATE courses 
        SET name = ? 
        WHERE code = ?
    """, (course_title, course_code))
    
    # 3. Link course to lecturer in lecturer_courses table
    cursor.execute("""
        INSERT OR IGNORE INTO lecturer_courses (lecturer_telegram_id, course_code)
        VALUES (?, ?)
    """, (str(telegram_id), course_code))
    
    conn.commit()
    conn.close()

def get_schedules_since(since_timestamp):
    """Retrieves all schedules created since a specific unix timestamp with lecturer UUID."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("""
        SELECT s.course_code, s.course_title, s.start_time, s.end_time, u.uuid as lecturer_uuid
        FROM schedules s
        LEFT JOIN users u ON s.telegram_id = u.telegram_id
        WHERE s.created_at > ?
        ORDER BY s.created_at ASC
    """, (int(since_timestamp),))
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]

def get_all_courses():
    """Retrieves all available courses."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("SELECT code, name FROM courses ORDER BY code ASC")
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]

def enroll_user_in_course(user_uuid, course_code):
    """Enrolls a user in a course. Creates the course if it doesn't exist. Returns 'ok', 'already_enrolled', or 'error'."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    try:
        # Automatically insert the course with a placeholder name if it doesn't exist yet
        cursor.execute("""
            INSERT OR IGNORE INTO courses (code, name)
            VALUES (?, ?)
        """, (course_code, f"Course {course_code}"))
        
        # Check if already enrolled
        cursor.execute("SELECT 1 FROM user_courses WHERE user_uuid = ? AND course_code = ?", (user_uuid, course_code))
        if cursor.fetchone():
            conn.close()
            return "already_enrolled"
        
        # Insert enrollment
        now = int(time.time())
        cursor.execute("""
            INSERT INTO user_courses (user_uuid, course_code, created_at)
            VALUES (?, ?, ?)
        """, (user_uuid, course_code, now))
        conn.commit()
        conn.close()
        return "ok"
    except Exception as e:
        logger.error(f"Error enrolling user in course: {e}")
        conn.close()
        return "error"

def get_enrollments_since(since_timestamp):
    """Retrieves all user-course enrollments created since a specific unix timestamp."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("""
        SELECT user_uuid, course_code 
        FROM user_courses 
        WHERE created_at > ?
        ORDER BY created_at ASC
    """, (int(since_timestamp),))
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]

def delete_user_by_uuid(uuid):
    """Deletes a user and their enrollments, and logs the deletion."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    try:
        # Get user details first if we need to log or return
        cursor.execute("SELECT name, student_id FROM users WHERE uuid = ?", (uuid,))
        user = cursor.fetchone()
        if not user:
            conn.close()
            return False
        
        # Delete enrollments
        cursor.execute("DELETE FROM user_courses WHERE user_uuid = ?", (uuid,))
        # Delete user
        cursor.execute("DELETE FROM users WHERE uuid = ?", (uuid,))
        # Log deletion
        now = int(time.time())
        cursor.execute("""
            INSERT OR REPLACE INTO deletions (user_uuid, deleted_at)
            VALUES (?, ?)
        """, (uuid, now))
        
        conn.commit()
        conn.close()
        return True
    except Exception as e:
        logger.error(f"Error deleting user {uuid}: {e}")
        conn.close()
        return False

def get_user_by_student_id(student_id):
    """Retrieves a user by their Student ID."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("""
        SELECT uuid, name, student_id, phone_number, telegram_id, role 
        FROM users 
        WHERE student_id = ?
    """, (student_id,))
    row = cursor.fetchone()
    conn.close()
    return dict(row) if row else None

def get_deletions_since(since_timestamp):
    """Retrieves all user UUIDs deleted since a specific unix timestamp."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute("""
        SELECT user_uuid 
        FROM deletions 
        WHERE deleted_at > ?
        ORDER BY deleted_at ASC
    """, (int(since_timestamp),))
    rows = cursor.fetchall()
    conn.close()
    return [r[0] for r in rows]

def reconcile_users(received_uuids):
    """Deletes users from the bot DB whose UUIDs are not in the received list (excluding dev users)."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    try:
        # Get all UUIDs in the database
        cursor.execute("SELECT uuid, role FROM users")
        db_users = cursor.fetchall()
        for row in db_users:
            db_uuid, role = row[0], row[1]
            # If it's a student (or user synced from device) and not in received list
            # and is NOT a dev user (we check if UUID starts with "dev-uuid-")
            if db_uuid not in received_uuids and not db_uuid.startswith("dev-uuid-"):
                logger.info(f"Reconciling: Deleting user {db_uuid} from bot DB (missing in sync payload)")
                cursor.execute("DELETE FROM user_courses WHERE user_uuid = ?", (db_uuid,))
                cursor.execute("DELETE FROM users WHERE uuid = ?", (db_uuid,))
        conn.commit()
        conn.close()
    except Exception as e:
        logger.error(f"Error during user reconciliation: {e}")
        conn.close()

def link_lecturer_course(telegram_id, course_code):
    """Links a course code to a lecturer's Telegram ID."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute("""
        INSERT OR IGNORE INTO lecturer_courses (lecturer_telegram_id, course_code)
        VALUES (?, ?)
    """, (str(telegram_id), course_code))
    conn.commit()
    conn.close()

def get_lecturer_courses(telegram_id):
    """Retrieves all courses assigned to a lecturer."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("""
        SELECT lc.course_code, COALESCE(c.name, 'Course ' || lc.course_code) as name
        FROM lecturer_courses lc
        LEFT JOIN courses c ON lc.course_code = c.code
        WHERE lc.lecturer_telegram_id = ?
    """, (str(telegram_id),))
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]

def add_report_request(course_code, telegram_id):
    """Queues a new report request for a course."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    now = int(time.time())
    cursor.execute("""
        INSERT INTO report_requests (course_code, lecturer_telegram_id, status, created_at)
        VALUES (?, ?, 'pending', ?)
    """, (course_code, str(telegram_id), now))
    conn.commit()
    conn.close()

def get_pending_report_requests():
    """Retrieves all pending report requests."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("""
        SELECT id as request_id, course_code, lecturer_telegram_id 
        FROM report_requests 
        WHERE status = 'pending'
    """)
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]

def complete_report_request(request_id):
    """Marks a report request as completed."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute("""
        UPDATE report_requests 
        SET status = 'completed' 
        WHERE id = ?
    """, (int(request_id),))
    conn.commit()
    conn.close()

def get_admin_telegram_id():
    """Retrieves the Telegram ID of the registered admin."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute("""
        SELECT telegram_id 
        FROM users 
        WHERE role = 'admin' AND telegram_id IS NOT NULL AND telegram_id != '' 
        LIMIT 1
    """)
    row = cursor.fetchone()
    conn.close()
    return row[0] if row else None
