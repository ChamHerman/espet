"""
Test script for verifying that POST /api/voice/tts synthesizes full WAV audio
and returns the extracted OLED key words in X-OLED-Line1 and X-OLED-Line2 headers.
"""

import sys
from pathlib import Path

# Add backend directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

from fastapi.testclient import TestClient
from main import app
from config import settings

client = TestClient(app)

def test_oled_tts_headers():
    test_cases = [
        (
            "I have turned on the status LED on GPIO 42 for you!",
            "Status LED",
            "ON",
        ),
        (
            "The weather in Kuala Lumpur today is 31 degrees and sunny with clear skies.",
            "Kuala Lumpur",
            "31C",
        ),
        (
            "I captured a snapshot from the ESP32 camera and uploaded it to Telegram.",
            "Photo",
            "Telegram",
        ),
    ]

    headers = {"Authorization": f"Bearer {settings.device_api_key}"}

    for text, exp_l1_sub, exp_l2_sub in test_cases:
        print(f"\n--- Testing TTS for: '{text}' ---")
        resp = client.post("/api/voice/tts", json={"text": text}, headers=headers)
        assert resp.status_code == 200, f"Expected 200 but got {resp.status_code}: {resp.text}"

        l1 = resp.headers.get("x-oled-line1", "")
        l2 = resp.headers.get("x-oled-line2", "")
        wav_len = len(resp.content)

        print(f"Status: {resp.status_code}")
        print(f"X-OLED-Line1: '{l1}' (length: {len(l1)} <= 16)")
        print(f"X-OLED-Line2: '{l2}' (length: {len(l2)} <= 16)")
        print(f"WAV Audio Size: {wav_len} bytes")

        assert len(l1) <= 16, f"Line 1 exceeds 16 chars: '{l1}'"
        assert len(l2) <= 16, f"Line 2 exceeds 16 chars: '{l2}'"
        assert wav_len > 44, "Audio WAV must be valid audio file > 44 bytes header"
        assert exp_l1_sub.lower() in l1.lower(), f"Expected '{exp_l1_sub}' in line 1 '{l1}'"
        assert exp_l2_sub.lower() in l2.lower(), f"Expected '{exp_l2_sub}' in line 2 '{l2}'"

    print("\n✅ ALL TTS + OLED HEADER TESTS PASSED!")

if __name__ == "__main__":
    test_oled_tts_headers()
