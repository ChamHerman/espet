import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from fastapi.testclient import TestClient
import main


class TestTTS(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.client = TestClient(main.app)

    def test_tts_valid_wav(self):
        resp = self.client.post(
            "/api/voice/tts",
            json={"text": "Hello test"},
            headers={"Authorization": f"Bearer {main.settings.device_api_key}"},
        )
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.headers.get("content-type"), "audio/wav")
        self.assertGreater(len(resp.content), 44)
        self.assertEqual(resp.content[:4], b"RIFF")
        self.assertEqual(resp.content[8:12], b"WAVE")


if __name__ == "__main__":
    unittest.main()
