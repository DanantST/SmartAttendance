#!/usr/bin/env python3
"""
Generate WAV audio files for SmartAttendance system prompts.
Creates placeholder WAV files with proper headers for the audio system.
"""

import os
import struct
import sys

def create_wav_file(filename, duration_ms=500, sample_rate=16000, channels=1, bits_per_sample=16):
    """Create a simple WAV file with silence (placeholder for real audio)."""
    
    # Calculate parameters
    num_samples = (sample_rate * duration_ms) // 1000
    byte_rate = sample_rate * channels * bits_per_sample // 8
    block_align = channels * bits_per_sample // 8
    audio_data_size = num_samples * block_align
    
    # Create WAV header
    wav_header = bytearray()
    
    # RIFF header
    wav_header.extend(b'RIFF')
    wav_header.extend(struct.pack('<I', 36 + audio_data_size))  # File size - 8
    wav_header.extend(b'WAVE')
    
    # fmt subchunk
    wav_header.extend(b'fmt ')
    wav_header.extend(struct.pack('<I', 16))  # Subchunk1Size
    wav_header.extend(struct.pack('<H', 1))   # AudioFormat (1 = PCM)
    wav_header.extend(struct.pack('<H', channels))
    wav_header.extend(struct.pack('<I', sample_rate))
    wav_header.extend(struct.pack('<I', byte_rate))
    wav_header.extend(struct.pack('<H', block_align))
    wav_header.extend(struct.pack('<H', bits_per_sample))
    
    # data subchunk
    wav_header.extend(b'data')
    wav_header.extend(struct.pack('<I', audio_data_size))
    
    # Write file
    os.makedirs(os.path.dirname(filename), exist_ok=True)
    with open(filename, 'wb') as f:
        f.write(wav_header)
        # Write silence (zeros)
        f.write(b'\x00' * audio_data_size)
    
    print(f"✓ Created {filename} ({audio_data_size} bytes of audio data)")

def main():
    # Create /sdcard/audio directory and generate placeholder files
    audio_dir = "/sdcard/audio"
    
    # List of required audio files
    audio_files = {
        "system_start.wav": 500,
        "system_shutdown.wav": 500,
        "attendance_success.wav": 400,
        "unknown_face.wav": 400,
        "enroll_start.wav": 300,
        "enroll_success.wav": 400,
        "enroll_fail.wav": 400,
        "low_battery.wav": 300,
    }
    
    print("SmartAttendance Audio File Generator")
    print("=" * 50)
    print(f"\nGenerating audio files in: {audio_dir}\n")
    
    # Generate each file
    for filename, duration_ms in audio_files.items():
        filepath = os.path.join(audio_dir, filename)
        create_wav_file(filepath, duration_ms=duration_ms)
    
    print("\n" + "=" * 50)
    print("All audio files generated successfully!")
    print("\nNote: These are placeholder files with silence.")
    print("Replace them with real audio files for actual voice prompts.")
    print("\nFor Windows SD card emulation, copy files to your emulated SD card path.")

if __name__ == "__main__":
    main()
