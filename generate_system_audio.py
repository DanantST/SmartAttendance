#!/usr/bin/env python3
"""
Generate placeholder system audio WAV files for SPIFFS embedding.
Creates 16kHz, 16-bit mono silence files that can be replaced with real audio.
"""

import os
import struct
import wave

def create_silence_wav(filename, duration_ms=500):
    """Create a simple silence WAV file."""
    sample_rate = 16000  # 16 kHz matching config.h
    num_samples = int(sample_rate * duration_ms / 1000)
    
    os.makedirs(os.path.dirname(filename), exist_ok=True)
    
    with wave.open(filename, 'wb') as wav_file:
        # Configure WAV: 1 channel (mono), 2 bytes per sample (16-bit), 16kHz
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)  # 2 bytes = 16-bit
        wav_file.setframerate(sample_rate)
        
        # Write silence (zeros)
        silence_data = struct.pack('<h', 0) * num_samples
        wav_file.writeframes(silence_data)
    
    print(f"✓ Created {filename} ({duration_ms}ms silence)")

# Create audio files matching the ones referenced in main.c and ui_player.c
audio_files = {
    'system_start.wav': 300,       # Startup beep (short)
    'system_shutdown.wav': 300,    # Shutdown sound (short)
    'attendance_success.wav': 500,  # Success confirmation
    'unknown_face.wav': 400,        # Warning sound
    'enroll_start.wav': 300,        # Enrollment start prompt
    'enroll_success.wav': 600,      # Enrollment successful
    'enroll_fail.wav': 600,         # Enrollment failed
    'low_battery.wav': 400,         # Battery warning
    'look_straight.wav': 400,       # Face detection prompt
}

spiffs_audio_dir = r'main\spiffs\audio'

print("Generating system audio files for SPIFFS embedding...")
print(f"Output directory: {spiffs_audio_dir}\n")

for filename, duration in audio_files.items():
    filepath = os.path.join(spiffs_audio_dir, filename)
    create_silence_wav(filepath, duration)

print("\n✓ All audio files generated successfully!")
print("\nNOTE: These are placeholder silence files. Replace them with real audio:")
print("  - Each file must be WAV format (16kHz, 16-bit mono/stereo)")
print("  - Files will be embedded into the device flash during build")
print("  - Accessible at runtime via /spiffs/audio/filename.wav")
