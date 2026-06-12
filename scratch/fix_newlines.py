import os
import glob

ui_dir = "main/ui"
files = glob.glob(os.path.join(ui_dir, "*.cpp"))

for filepath in files:
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()
    
    if r"\n" in content:
        content = content.replace(r"\n", "\n")
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"Fixed newlines in {filepath}")
