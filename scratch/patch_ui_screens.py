import os
import re

ui_dir = "main/ui"
files = ["ui_user_manager.cpp", "ui_reports.cpp", "ui_recorder.cpp", "ui_file_manager.cpp"]

for filename in files:
    filepath = os.path.join(ui_dir, filename)
    if not os.path.exists(filepath):
        continue
    
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()
    
    # 1. Change lv_scr_load_anim to lv_scr_load
    content = re.sub(r'lv_scr_load_anim\s*\(\s*([^,]+),\s*LV_SCR_LOAD_ANIM_[^,]+,\s*\d+,\s*\d+,\s*(?:false|true)\s*\);', r'lv_scr_load(\1);', content)
    
    # 2. Fix ui_close_* functions to not set screen pointers to NULL
    # Find all assignments to NULL inside the close function
    close_func_match = re.search(r'void\s+ui_close_[a-z_]+\s*\(\s*void\s*\)\s*\{', content)
    if close_func_match:
        start_idx = close_func_match.end()
        end_idx = content.find('ui_return_to_main', start_idx)
        if end_idx != -1:
            close_body = content[start_idx:end_idx]
            # Replace all `s_something = NULL;` with nothing
            new_close_body = re.sub(r's_[a-zA-Z0-9_]+\s*=\s*NULL;', '', close_body)
            # Replace all `heap_caps_free(...)` and their ifs with nothing
            new_close_body = re.sub(r'if\s*\([^)]+\)\s*\{\s*heap_caps_free[^}]+\}', '', new_close_body)
            content = content[:start_idx] + new_close_body + content[end_idx:]

    # 3. Fix ui_show_* functions to use lv_scr_load and return
    # Find if (s_something_screen) return;
    show_match = re.search(r'if\s*\((s_[a-zA-Z0-9_]+screen)\)\s*return;', content)
    if show_match:
        screen_var = show_match.group(1)
        replacement = f"if ({screen_var}) {{\\n        lv_scr_load({screen_var});\\n        return;\\n    }}"
        content = content.replace(show_match.group(0), replacement)
        
    with open(filepath, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"Patched {filename}")

