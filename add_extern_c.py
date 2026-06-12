import os
import glob
import re

def process_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Check if already has extern "C"
    if 'extern "C"' in content:
        print(f"Skipping {filepath} (already has extern C)")
        return
        
    lines = content.split('\n')
    
    # Find the #define guard
    guard_line_idx = -1
    for i, line in enumerate(lines):
        if re.match(r'^#define\s+[A-Za-z0-9_]+_H\b', line, re.IGNORECASE) or re.match(r'^#define\s+[A-Za-z0-9_]+_h\b', line):
            guard_line_idx = i
            break
            
    if guard_line_idx == -1:
        print(f"Warning: No header guard found in {filepath}")
        return
        
    # Find the last #endif
    endif_line_idx = -1
    for i in range(len(lines)-1, -1, -1):
        if lines[i].startswith('#endif'):
            endif_line_idx = i
            break
            
    if endif_line_idx == -1:
        print(f"Warning: No ending #endif found in {filepath}")
        return
        
    extern_c_start = [
        "",
        "#ifdef __cplusplus",
        "extern \"C\" {",
        "#endif",
        ""
    ]
    
    extern_c_end = [
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        ""
    ]
    
    # Insert end first so indices don't shift
    lines = lines[:endif_line_idx] + extern_c_end + lines[endif_line_idx:]
    
    # Insert start after the guard line
    lines = lines[:guard_line_idx+1] + extern_c_start + lines[guard_line_idx+1:]
    
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
        
    print(f"Updated {filepath}")

def main():
    base_dir = r"c:/Users/user/Documents/projects/SmartAttendance/main"
    header_files = glob.glob(os.path.join(base_dir, "**/*.h"), recursive=True)
    for h_file in header_files:
        process_file(h_file)

if __name__ == "__main__":
    main()
