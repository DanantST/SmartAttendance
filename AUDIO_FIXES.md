# Audio System Fixes - Summary

## Changes Made

### 1. **Recording Path & Folder Creation** ✅
**Files Modified:** `main/ui/ui_recorder.cpp`

- **Added headers:** `<sys/stat.h>` and `<errno.h>` for directory operations
- **Added `RECORDINGS_DIR` constant:** `/sdcard/recordings`
- **Added `ensure_recordings_directory()` function:** Creates the recordings folder if it doesn't exist using `mkdir()`
- **Updated recording path:** Changed from `/sdcard/rec_*.wav` to `/sdcard/recordings/rec_*.wav`
- **Fixed buffer size:** Increased filename buffer from 128 to 256 bytes for longer paths
- **Updated status messages:** Now correctly shows "Recording saved to /sdcard/recordings/"
- **Improved I2S PDM read:** Fixed timeout logic from problematic ternary operator to proper `pdMS_TO_TICKS(1000)`

### 2. **Audio Event Files Directory Creation** ✅
**Files Modified:** `main/audio/audio_player.c`

- **Added headers:** `<sys/stat.h>` and `<errno.h>` for directory operations
- **Added `ensure_audio_directory()` function:** Creates `/sdcard/audio/` if it doesn't exist
- **Called in `audio_init()`:** Ensures directory exists when audio system initializes
- **Improved error handling:** Changed log levels from LOGE to LOGW for missing files (graceful degradation)
- **Added file status logging:** Logs when audio files are successfully played

### 3. **Configuration Updates** ✅
**Files Modified:** `main/config.h`

- **Added `RECORDINGS_PATH` definition:** `/sdcard/recordings/` for consistency
- Centralizes all path definitions in one configuration file

### 4. **Audio File Generator Utility** ✅
**New File Created:** `generate_audio_files.py`

This script generates placeholder WAV audio files for all system prompts:
- `system_start.wav` (500ms)
- `system_shutdown.wav` (500ms)
- `attendance_success.wav` (400ms)
- `unknown_face.wav` (400ms)
- `enroll_start.wav` (300ms)
- `enroll_success.wav` (400ms)
- `enroll_fail.wav` (400ms)
- `low_battery.wav` (300ms)

## Audio System Architecture

### Playback Path (Speaker Output)
```
I2S_NUM_0 → GPIO_NUM_23 (AUDIO_I2S_DOUT) → NS4168 Codec → Speaker
```

### Recording Path (Microphone Input)
```
Microphone → GPIO_NUM_24 (MIC_PDM_CLK), GPIO_NUM_26 (MIC_PDM_DATA) → I2S_NUM_1
```

### Directory Structure
```
/sdcard/
├── audio/                    # Audio prompt files (created automatically)
│   ├── system_start.wav
│   ├── attendance_success.wav
│   ├── unknown_face.wav
│   ├── enroll_success.wav
│   ├── enroll_fail.wav
│   ├── enroll_start.wav
│   ├── low_battery.wav
│   └── system_shutdown.wav
├── recordings/              # User recordings (created automatically)
│   ├── rec_20260526_120530.wav
│   ├── rec_20260526_120645.wav
│   └── ...
└── attendance.db
```

## What's Fixed

### ✅ Recordings Folder
- Recordings now save to `/sdcard/recordings/` instead of SD card root
- Folder is automatically created if it doesn't exist
- No manual folder creation needed

### ✅ Audio Event Files
- System automatically creates `/sdcard/audio/` directory on startup
- Audio playback gracefully handles missing files (won't crash)
- Logs warnings instead of errors when files are missing

### ✅ Error Handling
- Missing audio files no longer cause system crashes
- Improved logging for debugging audio issues
- Better resource cleanup in recording task

## What Still Needs To Be Done

### 1. **Generate Audio Files (One-Time Setup)**
```bash
python generate_audio_files.py
```
This creates placeholder WAV files on your PC's emulated SD card.

Or manually:
- Create `/sdcard/audio/` folder
- Add placeholder or real audio files (16-bit, 16kHz WAV format)

### 2. **Optional: Add Real Audio Files**
Replace the placeholder files with actual voice prompts:
- Record or synthesize audio files
- Ensure format: **PCM, 16-bit, 16kHz, mono or stereo**
- Place in `/sdcard/audio/` directory

### 3. **Testing & Verification**
After building the project:
1. Flash the firmware to the device
2. Check the `/sdcard/recordings/` folder - should be created automatically
3. Try recording - files should appear in `/sdcard/recordings/`
4. Check `/sdcard/audio/` folder - should be created automatically
5. Monitor ESP32 logs for any I2S audio errors

### 4. **Troubleshooting Audio Recording Issues**

If audio recording still doesn't work:

**Check Microphone Hardware:**
```c
// In ui_recorder.cpp, the microphone uses:
#define MIC_PDM_CLK   GPIO_NUM_24    // PDM clock output
#define MIC_PDM_DATA  GPIO_NUM_26    // PDM data input

// Verify these pins are correct for your board
```

**Verify I2S Configuration:**
- I2S clock: GPIO_NUM_24 (PDM mode)
- I2S data: GPIO_NUM_26 (PDM mode)
- I2S channel: I2S_NUM_1 (to avoid conflict with speaker on I2S_NUM_0)

**Check Device Logs:**
- Look for "Recording saved, XXX bytes" in ESP32 logs
- If bytes are 0 or very small, microphone may not be connected

**Increase Debug Logging:**
Uncomment in ui_recorder.cpp record_task():
```c
ESP_LOGD(TAG, "Recorded %zu bytes, total: %lu", bytes_read, total_bytes);
```

## File Modifications Summary

| File | Changes |
|------|---------|
| `main/ui/ui_recorder.cpp` | Added folder creation, fixed path, improved I2S read |
| `main/audio/audio_player.c` | Added directory creation, improved error handling |
| `main/config.h` | Added RECORDINGS_PATH definition |
| `generate_audio_files.py` | NEW - Audio file generator utility |

## Next Steps

1. **Build the project:** The code changes are complete and ready to build
2. **Generate audio files:** Run `python generate_audio_files.py` on your PC
3. **Flash to device:** Use ESP-IDF to flash the updated firmware
4. **Test recording:** Use the Sound Recorder UI screen to verify
5. **Monitor logs:** Check device logs for any audio-related errors

## Important Notes

- The audio system is resilient and won't crash if audio files are missing
- Placeholder audio files are minimal (silence) but have proper WAV headers
- The microphone recording depends on hardware being present and properly connected
- All paths are automatically created if missing - no manual setup needed
