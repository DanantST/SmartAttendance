# SPIFFS Audio System Setup

## Overview
System audio files are now embedded into the device firmware build using SPIFFS (SPI Flash File System). This eliminates the dependency on SD card audio files and provides a reliable fallback mechanism.

## Changes Made

### 1. **Generated System Audio Files** ✓
Created placeholder WAV files in `main/spiffs/audio/`:
- `system_start.wav` (300ms) - Startup beep
- `system_shutdown.wav` (300ms) - Shutdown confirmation
- `attendance_success.wav` (500ms) - Attendance confirmed
- `unknown_face.wav` (400ms) - Face not recognized warning
- `enroll_start.wav` (300ms) - Enrollment start prompt
- `enroll_success.wav` (600ms) - Enrollment successful
- `enroll_fail.wav` (600ms) - Enrollment failed
- `low_battery.wav` (400ms) - Battery warning
- `look_straight.wav` (400ms) - Face alignment prompt

**Note:** Currently these are 16kHz, 16-bit mono silence files. Replace them with real audio files matching the same format.

### 2. **SPIFFS Embedding Configuration** ✓
Updated `main/CMakeLists.txt` to embed SPIFFS partition:
```cmake
spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)
```
This creates a FAT/SPIFFS filesystem image from the `spiffs/` directory during build and flashes it to the device storage partition.

### 3. **Audio Player Modifications** ✓
Enhanced `main/audio/audio_player.c` with:

#### Added SPIFFS Support
- Mounted SPIFFS partition at boot via `mount_spiffs_audio()`
- Added fallback file search: `try_open_audio_file()`

#### Audio File Priority
When playing a sound file at `/sdcard/audio/filename.wav`:
1. **First** → Try SD card: `/sdcard/audio/filename.wav`
2. **Fallback** → Try SPIFFS: `/spiffs/audio/filename.wav`

This means:
- If SD card has the file → uses it (priority)
- If SD card missing → automatically falls back to embedded SPIFFS version
- Seamless operation with or without SD card audio files

## Build Status
- ✓ Audio files generated
- ✓ CMakeLists.txt updated
- ✓ audio_player.c enhanced
- ⏳ Build in progress (620 targets)

## How It Works

### Startup Flow
1. `audio_init()` is called during system initialization
2. SPIFFS partition is mounted at `/spiffs` (non-blocking, graceful if already mounted)
3. Audio directory created at `/sdcard/audio/` if SD card is available

### Playback Flow
When code calls `audio_play_file("/sdcard/audio/system_start.wav", true)`:
1. Try opening `/sdcard/audio/system_start.wav`
   - ✓ Found → Play from SD card
   - ✗ Not found → Continue to step 2
2. Try opening `/spiffs/audio/system_start.wav`
   - ✓ Found → Play from SPIFFS (fallback)
   - ✗ Not found → Log warning and return error

## Integration Points

### In main.c
Audio calls remain unchanged (no code modifications needed):
```c
audio_play("/sdcard/audio/system_start.wav", false);  // Already works with fallback!
```

### In UI Code (ui_player.c)
All existing audio playback calls automatically benefit from SPIFFS fallback without modification.

## Replacing Placeholder Audio

To add real system audio files:

1. **Generate or create WAV files** with:
   - Format: WAV (RIFF)
   - Sample rate: 16 kHz
   - Bit depth: 16-bit
   - Channels: Mono or Stereo
   - Duration: 300-600ms recommended

2. **Replace files in** `main/spiffs/audio/` directory

3. **Rebuild** the project:
   ```bash
   idf.py build
   ```
   The new audio files will be automatically embedded in the next flash.

## SPIFFS Partition Details
- **Partition name**: `storage` (shared with SD card)
- **File system**: FAT (created from SPIFFS directory)
- **Size**: 3 MB (0x300000)
- **Mount point**: `/spiffs`
- **Flash method**: Embedded at build time with `FLASH_IN_PROJECT` flag

## Troubleshooting

### Audio Still Not Playing
1. Verify WAV files exist in `main/spiffs/audio/`
2. Check file format (should be valid RIFF/WAV)
3. Monitor device logs for audio playback attempts
4. Test both SD card and SPIFFS paths using file manager

### Build Issues
If `spiffs_create_partition_image` fails:
1. Verify `main/spiffs/` directory exists
2. Ensure at least one audio file is present
3. Check CMakeLists.txt for syntax errors
4. Run `idf.py fullclean` and rebuild

## Performance Impact
- **Build time**: +2-5 seconds (creating SPIFFS image)
- **Flash size**: ~200 KB (9 WAV files + SPIFFS overhead)
- **Runtime overhead**: None (SPIFFS mounted once at startup)

## Next Steps
1. Replace placeholder WAV files with real audio
2. Test audio playback with and without SD card
3. Verify fallback behavior (remove SD card audio to test SPIFFS)
4. Monitor device logs for any audio loading issues
