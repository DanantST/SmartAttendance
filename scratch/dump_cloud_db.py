import urllib.request
import json

url = "https://danantst-smart-attendance-bot.hf.space/api/dump_db"

print("Fetching cloud DB dump from:", url)
try:
    with urllib.request.urlopen(url, timeout=15) as response:
        status = response.getcode()
        body = response.read().decode('utf-8')
        data = json.loads(body)
        print("Status Code:", status)
        print("\n--- Users ---")
        for u in data.get("users", []):
            print(f"UUID: {u['uuid']} | Name: {u['name']} | Role: {u['role']} | Phone: {u['phone_number']} | Telegram ID: {u['telegram_id']}")
        print("\n--- Schedules ---")
        for s in data.get("schedules", []):
            print(s)
        print("\n--- Courses ---")
        for c in data.get("courses", []):
            print(c)
        print("\n--- Lecturer Courses ---")
        for lc in data.get("lecturer_courses", []):
            print(lc)
        print("\n--- User Courses ---")
        for uc in data.get("user_courses", []):
            print(uc)
        print("\n--- Pending Links ---")
        for pl in data.get("pending_links", []):
            print(pl)
except Exception as e:
    print("Error:", e)
