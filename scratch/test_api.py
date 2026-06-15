import urllib.request
import urllib.error
import json

url = "https://danantst-smart-attendance-bot.hf.space/api/sync_users"
payload = [
    {
        "uuid": "4",
        "name": "Prof John Doe",
        "student_id": "",
        "phone_number": "+234 8143324283",
        "telegram_id": "",
        "role": "lecturer"
    }
]

print("Sending request to:", url)
req = urllib.request.Request(
    url,
    data=json.dumps(payload).encode('utf-8'),
    headers={'Content-Type': 'application/json'}
)

try:
    with urllib.request.urlopen(req, timeout=15) as response:
        status = response.getcode()
        body = response.read().decode('utf-8')
        print("Status Code:", status)
        print("Response text:")
        print(body)
except urllib.error.HTTPError as e:
    print("HTTP Error:", e.code)
    print(e.read().decode('utf-8'))
except Exception as e:
    print("Error:", e)
