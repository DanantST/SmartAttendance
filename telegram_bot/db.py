import sqlite3
import logging
import time
import os

logger = logging.getLogger(__name__)

# Try to default to /data/bot_data.db if running in an environment (like HF Spaces) with persistent storage,
# otherwise fall back to DATABASE_PATH or local directory
if os.path.exists("/data") and os.access("/data", os.W_OK):
    DB_PATH = "/data/bot_data.db"
else:
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
    
    # Run migration to add event_type to schedules if it doesn't exist
    try:
        cursor.execute("ALTER TABLE schedules ADD COLUMN event_type TEXT DEFAULT 'lecture'")
    except sqlite3.OperationalError:
        pass
    
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

    # Table for pending user pairings (phone_number -> telegram_id)
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS pending_links (
        phone_number TEXT PRIMARY KEY,
        telegram_id TEXT NOT NULL,
        created_at INTEGER NOT NULL
    )
    """)
    
    # Table to track course deletions
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS course_deletions (
        course_code TEXT PRIMARY KEY,
        deleted_at INTEGER NOT NULL
    )
    """)

    # Table to track schedule deletions
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS schedule_deletions (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        course_code TEXT NOT NULL,
        start_time INTEGER NOT NULL,
        end_time INTEGER NOT NULL,
        deleted_at INTEGER NOT NULL
    )
    """)

    # Table to track enrollment deletions (student unsubscriptions)
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS enrollment_deletions (
        user_uuid TEXT NOT NULL,
        course_code TEXT NOT NULL,
        deleted_at INTEGER NOT NULL,
        PRIMARY KEY (user_uuid, course_code)
    )
    """)

    # Table to track lecturer course unassignments
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS lecturer_course_deletions (
        lecturer_telegram_id TEXT NOT NULL,
        course_code TEXT NOT NULL,
        deleted_at INTEGER NOT NULL,
        PRIMARY KEY (lecturer_telegram_id, course_code)
    )
    """)

    
    # Migration: reassign 'device' schedules to real lecturer telegram_ids via lecturer_courses
    try:
        cursor.execute("""
            UPDATE schedules
            SET telegram_id = (
                SELECT lc.lecturer_telegram_id
                FROM lecturer_courses lc
                WHERE lc.course_code = schedules.course_code
                  AND lc.lecturer_telegram_id IS NOT NULL
                  AND lc.lecturer_telegram_id != ''
                LIMIT 1
            )
            WHERE telegram_id = 'device'
              AND EXISTS (
                SELECT 1 FROM lecturer_courses lc2
                WHERE lc2.course_code = schedules.course_code
                  AND lc2.lecturer_telegram_id IS NOT NULL
                  AND lc2.lecturer_telegram_id != ''
              )
        """)
        if cursor.rowcount > 0:
            logger.info(f"Migration: reassigned {cursor.rowcount} 'device' schedule(s) to real lecturer IDs.")
    except Exception as e:
        logger.warning(f"Migration (device->lecturer telegram_id reassign) skipped: {e}")

    # Migration: add welcomed_at column to track first-time welcome messages
    try:
        cursor.execute("ALTER TABLE users ADD COLUMN welcomed_at INTEGER DEFAULT NULL")
        logger.info("Migration: added welcomed_at column to users table.")
    except sqlite3.OperationalError:
        pass  # Column already exists

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

def mark_user_welcomed(uuid):
    """Stamps welcomed_at for a user so they are not welcomed again on future syncs."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    now = int(time.time())
    cursor.execute("UPDATE users SET welcomed_at = ? WHERE uuid = ?", (now, uuid))
    conn.commit()
    conn.close()

def get_unwelcomed_linked_users():
    """
    Returns all users that:
      - have a non-empty telegram_id (they have linked their Telegram account), AND
      - have welcomed_at IS NULL (bot has never sent them a welcome message).
    Each returned dict has: uuid, name, role, telegram_id.
    """
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("""
        SELECT uuid, name, role, telegram_id
        FROM users
        WHERE telegram_id IS NOT NULL
          AND telegram_id != ''
          AND welcomed_at IS NULL
    """)
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]

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

def add_schedule(telegram_id, course_code, course_title, start_time, end_time, event_type="lecture"):
    """Creates a new schedule record and populates/updates the courses table."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    now = int(time.time())
    
    # 1. Insert course schedule
    cursor.execute("""
        INSERT INTO schedules (telegram_id, course_code, course_title, start_time, end_time, event_type, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    """, (str(telegram_id), course_code, course_title, int(start_time), int(end_time), event_type, now))
    
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
        SELECT s.course_code, s.course_title, s.start_time, s.end_time, s.event_type, u.uuid as lecturer_uuid
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
            # If it's a student and not in received list
            # and is NOT a dev user (we check if UUID starts with "dev-uuid-")
            if role == "student" and db_uuid not in received_uuids and not db_uuid.startswith("dev-uuid-"):
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

def add_pending_link(phone_number, telegram_id):
    """Saves a pending link mapping."""
    phone = normalize_phone(phone_number)
    if not phone or len(phone) < 9:
        return
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    now = int(time.time())
    cursor.execute("""
        INSERT OR REPLACE INTO pending_links (phone_number, telegram_id, created_at)
        VALUES (?, ?, ?)
    """, (phone[-9:], str(telegram_id), now))
    conn.commit()
    conn.close()

def get_pending_link(phone_number):
    """Retrieves a pending link telegram_id by phone number using suffix matching."""
    phone = normalize_phone(phone_number)
    if not phone or len(phone) < 9:
        return None
    suffix = phone[-9:]
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute("SELECT telegram_id FROM pending_links WHERE phone_number = ?", (suffix,))
    row = cursor.fetchone()
    conn.close()
    return row[0] if row else None

def delete_pending_link(phone_number):
    """Deletes a pending link."""
    phone = normalize_phone(phone_number)
    if not phone or len(phone) < 9:
        return
    suffix = phone[-9:]
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute("DELETE FROM pending_links WHERE phone_number = ?", (suffix,))
    conn.commit()
    conn.close()

def get_enrolled_students(course_code):
    """Gets all students enrolled in a course who have linked Telegram IDs."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("""
        SELECT u.telegram_id, u.name 
        FROM user_courses uc 
        JOIN users u ON uc.user_uuid = u.uuid 
        WHERE uc.course_code = ? AND u.telegram_id IS NOT NULL AND u.telegram_id != ''
    """, (course_code,))
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]

def get_lecturer_schedules(telegram_id):
    """
    Retrieves all schedules for a lecturer.
    This includes:
      - Schedules created directly via the bot (telegram_id = lecturer's ID)
      - Schedules uploaded from the device (telegram_id = 'device') for courses
        the lecturer owns (via lecturer_courses table)
    """
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("""
        SELECT id, course_code, course_title, start_time, end_time, event_type
        FROM schedules
        WHERE telegram_id = ?

        UNION

        SELECT s.id, s.course_code, s.course_title, s.start_time, s.end_time, s.event_type
        FROM schedules s
        JOIN lecturer_courses lc ON s.course_code = lc.course_code
        WHERE s.telegram_id = 'device'
          AND lc.lecturer_telegram_id = ?

        ORDER BY start_time ASC
    """, (str(telegram_id), str(telegram_id)))
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]

def delete_schedule(schedule_id):
    """Deletes a schedule by its ID and logs the deletion so the device can sync it."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    try:
        # Get schedule details to log deletion
        cursor.execute("SELECT course_code, start_time, end_time FROM schedules WHERE id = ?", (schedule_id,))
        row = cursor.fetchone()
        if not row:
            conn.close()
            return False
            
        course_code, start_time, end_time = row
        
        # Delete from schedules table
        cursor.execute("DELETE FROM schedules WHERE id = ?", (schedule_id,))
        
        # Log deletion
        now = int(time.time())
        cursor.execute("""
            INSERT INTO schedule_deletions (course_code, start_time, end_time, deleted_at)
            VALUES (?, ?, ?, ?)
        """, (course_code, start_time, end_time, now))
        
        conn.commit()
        conn.close()
        return True
    except Exception as e:
        logger.error(f"Error deleting schedule {schedule_id}: {e}")
        conn.close()
        return False

def delete_course(course_code):
    """Deletes a course globally, removing it from schedules, enrollments, and logging the deletion."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    try:
        now = int(time.time())

        # Log enrollment deletions for all students currently enrolled
        cursor.execute("SELECT user_uuid FROM user_courses WHERE course_code = ?", (course_code,))
        enrolled_users = cursor.fetchall()
        for eu in enrolled_users:
            cursor.execute("""
                INSERT OR REPLACE INTO enrollment_deletions (user_uuid, course_code, deleted_at)
                VALUES (?, ?, ?)
            """, (eu[0], course_code, now))

        # Log lecturer course link deletions
        cursor.execute("SELECT lecturer_telegram_id FROM lecturer_courses WHERE course_code = ?", (course_code,))
        assigned_lecturers = cursor.fetchall()
        for al in assigned_lecturers:
            cursor.execute("""
                INSERT OR REPLACE INTO lecturer_course_deletions (lecturer_telegram_id, course_code, deleted_at)
                VALUES (?, ?, ?)
            """, (al[0], course_code, now))

        # Delete from courses
        cursor.execute("DELETE FROM courses WHERE code = ?", (course_code,))
        # Delete from lecturer_courses
        cursor.execute("DELETE FROM lecturer_courses WHERE course_code = ?", (course_code,))
        # Delete from user_courses
        cursor.execute("DELETE FROM user_courses WHERE course_code = ?", (course_code,))
        
        # Log deletions for schedules associated with this course before deleting them
        cursor.execute("SELECT start_time, end_time FROM schedules WHERE course_code = ?", (course_code,))
        schedules_to_delete = cursor.fetchall()
        
        for s in schedules_to_delete:
            cursor.execute("""
                INSERT INTO schedule_deletions (course_code, start_time, end_time, deleted_at)
                VALUES (?, ?, ?, ?)
            """, (course_code, s[0], s[1], now))
            
        # Delete from schedules
        cursor.execute("DELETE FROM schedules WHERE course_code = ?", (course_code,))
        
        # Log course deletion
        cursor.execute("""
            INSERT OR REPLACE INTO course_deletions (course_code, deleted_at)
            VALUES (?, ?)
        """, (course_code, now))
        
        conn.commit()
        conn.close()
        return True
    except Exception as e:
        logger.error(f"Error deleting course {course_code}: {e}")
        conn.close()
        return False


def update_schedule_time(schedule_id, start_time, end_time):
    """Updates the schedule time and logs a deletion for the old time so the device syncs cleanly."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    try:
        # Fetch old details first
        cursor.execute("SELECT course_code, start_time, end_time FROM schedules WHERE id = ?", (schedule_id,))
        row = cursor.fetchone()
        if not row:
            conn.close()
            return False
            
        course_code, old_start, old_end = row
        
        # Log deletion for the old schedule slot
        now = int(time.time())
        cursor.execute("""
            INSERT INTO schedule_deletions (course_code, start_time, end_time, deleted_at)
            VALUES (?, ?, ?, ?)
        """, (course_code, old_start, old_end, now))
        
        # Update schedule with the new time and update created_at so the device pulls it as a new schedule entry
        cursor.execute("""
            UPDATE schedules 
            SET start_time = ?, end_time = ?, created_at = ?
            WHERE id = ?
        """, (int(start_time), int(end_time), now, schedule_id))
        
        conn.commit()
        conn.close()
        return True
    except Exception as e:
        logger.error(f"Error updating schedule {schedule_id}: {e}")
        conn.close()
        return False


# ==================== Bidirectional Sync Helpers ====================

def upsert_course_from_device(code, name):
    """Upserts a course received from the device into the cloud courses table."""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    try:
        cursor.execute("""
            INSERT OR IGNORE INTO courses (code, name)
            VALUES (?, ?)
        """, (code, name))
        conn.commit()
    except Exception as e:
        logger.error(f"Error upserting course from device ({code}): {e}")
    finally:
        conn.close()


def upsert_schedule_from_device(course_code, course_title, start_time, end_time, event_type='lecture'):
    """
    Upserts a schedule received from the device into the cloud schedules table.
    Tries to resolve the lecturer's real telegram_id via lecturer_courses so the
    schedule appears under the lecturer in My Schedules. Falls back to 'device'
    if the course is not yet linked to any lecturer.
    Deduplicates on (course_code, start_time, end_time).
    """
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    now = int(time.time())
    try:
        # Resolve the lecturer's telegram_id for this course (if already linked)
        cursor.execute("""
            SELECT lecturer_telegram_id FROM lecturer_courses
            WHERE course_code = ?
            LIMIT 1
        """, (course_code,))
        lc_row = cursor.fetchone()
        owner_telegram_id = lc_row[0] if lc_row and lc_row[0] else 'device'

        # Ensure the course exists first
        cursor.execute("""
            INSERT OR IGNORE INTO courses (code, name)
            VALUES (?, ?)
        """, (course_code, course_title))

        # Skip re-inserting a schedule that the bot already deleted (prevents re-appear after device sync)
        cursor.execute("""
            SELECT 1 FROM schedule_deletions
            WHERE course_code = ? AND start_time = ? AND end_time = ?
        """, (course_code, int(start_time), int(end_time)))
        if cursor.fetchone():
            logger.debug(f"upsert_schedule_from_device: skipping {course_code} {start_time}-{end_time} (already deleted by bot)")
            conn.commit()
            return

        # Insert only if this exact slot doesn't already exist
        cursor.execute("""
            INSERT INTO schedules (telegram_id, course_code, course_title, start_time, end_time, event_type, created_at)
            SELECT ?, ?, ?, ?, ?, ?, ?
            WHERE NOT EXISTS (
                SELECT 1 FROM schedules
                WHERE course_code = ? AND start_time = ? AND end_time = ?
            )
        """, (owner_telegram_id, course_code, course_title, int(start_time), int(end_time), event_type, now,
              course_code, int(start_time), int(end_time)))
        conn.commit()
    except Exception as e:
        logger.error(f"Error upserting schedule from device ({course_code}): {e}")
    finally:
        conn.close()


def upsert_lecturer_course_from_device(lecturer_uuid, course_code):
    """
    Links a lecturer to a course in the bot DB based on the device's lecturer_courses data.
    Looks up the lecturer by their UUID to find their Telegram ID, then inserts the link.
    """
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    try:
        cursor.execute("SELECT telegram_id FROM users WHERE uuid = ?", (lecturer_uuid,))
        row = cursor.fetchone()
        if row and row[0]:
            telegram_id = row[0]
            cursor.execute("""
                INSERT OR IGNORE INTO lecturer_courses (lecturer_telegram_id, course_code)
                VALUES (?, ?)
            """, (telegram_id, course_code))
            conn.commit()
    except Exception as e:
        logger.error(f"Error upserting lecturer course from device (uuid={lecturer_uuid}): {e}")
    finally:
        conn.close()


def get_cloud_schedules_not_known_by_device(known_keys):
    """
    Returns schedules the cloud has that the device doesn't.
    known_keys: set of (course_code, start_time, end_time) tuples sent by the device.
    """
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("""
        SELECT s.course_code, s.course_title, s.start_time, s.end_time,
               COALESCE(s.event_type, 'lecture') AS event_type,
               u.uuid AS lecturer_uuid
        FROM schedules s
        LEFT JOIN users u ON s.telegram_id = u.telegram_id
        ORDER BY s.start_time ASC
    """)
    rows = cursor.fetchall()
    conn.close()
    result = []
    for r in rows:
        key = (r['course_code'], r['start_time'], r['end_time'])
        if key not in known_keys:
            result.append(dict(r))
    return result


def get_cloud_courses_not_known_by_device(known_codes):
    """Returns courses the cloud has that the device doesn't (compared by course code)."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("SELECT code, name FROM courses")
    rows = cursor.fetchall()
    conn.close()
    return [{"code": r["code"], "name": r["name"]} for r in rows if r["code"] not in known_codes]


def get_cloud_lecturer_assignments_not_known_by_device(known_pairs):
    """
    Returns lecturer→course assignments the cloud has that the device doesn't.
    known_pairs: set of (lecturer_uuid, course_code) tuples sent by the device.
    """
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("""
        SELECT u.uuid AS lecturer_uuid, lc.course_code
        FROM lecturer_courses lc
        JOIN users u ON lc.lecturer_telegram_id = u.telegram_id
        WHERE u.uuid IS NOT NULL AND u.uuid != ''
    """)
    rows = cursor.fetchall()
    conn.close()
    result = []
    for r in rows:
        pair = (r['lecturer_uuid'], r['course_code'])
        if pair not in known_pairs:
            result.append({"lecturer_uuid": r["lecturer_uuid"], "course_code": r["course_code"]})
    return result


def remove_lecturer_course(lecturer_telegram_id, course_code):
    """
    Removes a lecturer→course assignment and logs the removal in lecturer_course_deletions
    so the device can pick up the change on its next sync.
    """
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    try:
        now = int(time.time())
        # Log before removing so the device can sync the deletion
        cursor.execute("""
            INSERT OR REPLACE INTO lecturer_course_deletions (lecturer_telegram_id, course_code, deleted_at)
            VALUES (?, ?, ?)
        """, (str(lecturer_telegram_id), course_code, now))
        cursor.execute("""
            DELETE FROM lecturer_courses WHERE lecturer_telegram_id = ? AND course_code = ?
        """, (str(lecturer_telegram_id), course_code))
        conn.commit()
        conn.close()
        return True
    except Exception as e:
        logger.error(f"Error removing lecturer course ({lecturer_telegram_id}, {course_code}): {e}")
        conn.close()
        return False


def unenroll_user_from_course(user_uuid, course_code):
    """
    Removes a student's enrollment in a specific course and logs the removal in
    enrollment_deletions so the device can pick up the change on its next sync.
    Returns True on success, False on error.
    """
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    try:
        now = int(time.time())
        # Log before removing so the device can sync the deletion
        cursor.execute("""
            INSERT OR REPLACE INTO enrollment_deletions (user_uuid, course_code, deleted_at)
            VALUES (?, ?, ?)
        """, (user_uuid, course_code, now))
        cursor.execute("""
            DELETE FROM user_courses WHERE user_uuid = ? AND course_code = ?
        """, (user_uuid, course_code))
        conn.commit()
        conn.close()
        return True
    except Exception as e:
        logger.error(f"Error unenrolling user {user_uuid} from course {course_code}: {e}")
        conn.close()
        return False


def get_enrollment_deletions_since(since_timestamp):
    """Returns {user_uuid, course_code} pairs of unenrollments since a given timestamp."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("""
        SELECT user_uuid, course_code
        FROM enrollment_deletions
        WHERE deleted_at > ?
        ORDER BY deleted_at ASC
    """, (int(since_timestamp),))
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]


def get_lecturer_course_deletions_since(since_timestamp):
    """Returns {lecturer_uuid, course_code} pairs of lecturer unassignments since a given timestamp."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()
    cursor.execute("""
        SELECT u.uuid AS lecturer_uuid, lcd.course_code
        FROM lecturer_course_deletions lcd
        JOIN users u ON lcd.lecturer_telegram_id = u.telegram_id
        WHERE lcd.deleted_at > ?
        ORDER BY lcd.deleted_at ASC
    """, (int(since_timestamp),))
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]
