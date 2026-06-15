import sqlite3
import os

db_path = os.path.join("telegram_bot", "bot_data.db")
print("Database file path:", os.path.abspath(db_path))
print("File size:", os.path.getsize(db_path) if os.path.exists(db_path) else "Does not exist")

conn = sqlite3.connect(db_path)
conn.row_factory = sqlite3.Row
cursor = conn.cursor()

# Get tables
cursor.execute("SELECT name FROM sqlite_master WHERE type='table'")
tables = [r[0] for r in cursor.fetchall()]
print("Tables in database:", tables)

for table in tables:
    cursor.execute(f"SELECT COUNT(*) FROM {table}")
    count = cursor.fetchone()[0]
    print(f"Table '{table}' has {count} rows")
    
    if count > 0:
        cursor.execute(f"SELECT * FROM {table} LIMIT 10")
        print(f"--- Rows in '{table}' (max 10) ---")
        for row in cursor.fetchall():
            print(dict(row))
        print("-------------------------------")

conn.close()
