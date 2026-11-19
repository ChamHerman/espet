import unittest
from unittest.mock import patch, AsyncMock
from services.oled_summarizer import (
    extract_oled_keywords,
    extract_oled_keywords_llm,
    _sanitize_oled_line,
    MAX_LINE_CHARS,
)
from services.telegram_gateway import TelegramGateway

class TestOledSummarizer(unittest.TestCase):
    def test_empty_and_short(self):
        self.assertEqual(extract_oled_keywords(""), ("", ""))
        l1, l2 = extract_oled_keywords("Hello ESPET")
        self.assertEqual(l1, "Hello ESPET")
        self.assertEqual(l2, "")

    def test_line_length_invariant(self):
        samples = [
            "I have turned on the status LED for you, please let me know if you need anything else!",
            "Sure thing! I have switched off the white LED on GPIO pin 42.",
            "The weather in Kuala Lumpur today is 32 degrees Celsius and partly cloudy with chance of rain.",
            "📸 Snapshot taken successfully! Here is the photo from the ESP32 camera sent to Telegram.",
            "Artificial intelligence is intelligence demonstrated by machines, as opposed to natural intelligence.",
            "Added a calendar event for tomorrow at 3:00 PM for project discussion with supervisor.",
            "Current time is 15:45:00 UTC.",
            "GPIO 40 has been set to HIGH successfully.",
            "Here is the detailed result of the web search regarding quantum computing advancements in 2026.",
            "****************************************************************",
            "A" * 100,
            "Supercalifragilisticexpialidocious is an extremely long word",
        ]
        for text in samples:
            l1, l2 = extract_oled_keywords(text)
            self.assertLessEqual(len(l1), MAX_LINE_CHARS, f"Line 1 too long ({len(l1)}): '{l1}' from '{text}'")
            self.assertLessEqual(len(l2), MAX_LINE_CHARS, f"Line 2 too long ({len(l2)}): '{l2}' from '{text}'")
            self.assertNotIn("\n", l1)
            self.assertNotIn("\r", l1)
            self.assertNotIn("\n", l2)
            self.assertNotIn("\r", l2)

    def test_hardware_led_intent(self):
        l1, l2 = extract_oled_keywords("I've turned on the white LED on GPIO 42")
        self.assertIn("LED", l1)
        self.assertIn("ON", l2)
        self.assertLessEqual(len(l1), 16)
        self.assertLessEqual(len(l2), 16)

        l1, l2 = extract_oled_keywords("The RGB LED has been turned off")
        self.assertIn("RGB LED", l1)
        self.assertIn("OFF", l2)

    def test_camera_intent(self):
        l1, l2 = extract_oled_keywords("I captured a photo and sent it to Telegram")
        self.assertIn("Photo", l1)
        self.assertIn("Telegram", l2)

    def test_weather_intent(self):
        l1, l2 = extract_oled_keywords("The weather in Kuala Lumpur is 31 degrees and rain")
        self.assertIn("Kuala Lumpur", l1)
        self.assertTrue("31C" in l2 or "Rain" in l2)

    def test_general_prose_no_word_splitting(self):
        text = "Machine learning algorithms build a mathematical model based on sample data"
        l1, l2 = extract_oled_keywords(text)
        self.assertLessEqual(len(l1), 16)
        self.assertLessEqual(len(l2), 16)
        words_l1 = l1.split()
        for w in words_l1:
            self.assertTrue(w in text or w.capitalize() in text or w.lower() in text.lower())

    def test_sanitize_oled_line(self):
        self.assertEqual(_sanitize_oled_line(""), "")
        self.assertEqual(_sanitize_oled_line("**Hello World!**"), "Hello World!")
        # Exceeds 16 chars
        res = _sanitize_oled_line("Supercalifragilisticexpialidocious")
        self.assertLessEqual(len(res), 16)
        self.assertEqual(res, "Supercalifragili")
        # Non-ASCII unicode gets stripped/normalized
        res_unicode = _sanitize_oled_line("你好世界 Hello")
        self.assertEqual(res_unicode, "Hello")

    def test_telegram_gateway_alias(self):
        gw = TelegramGateway(registry=None, bot_token="fake:token")
        self.assertTrue(hasattr(gw, "send_message"))
        self.assertEqual(gw.send_message, gw.send_text)

    async def _run_llm_summarizer(self):
        import httpx
        mock_resp = httpx.Response(
            200,
            json={"choices": [{"message": {"content": "Seeing Pale Wall\nHand In Front"}}]},
            request=httpx.Request("POST", "https://api.test.com/v1/chat/completions"),
        )

        with patch("services.gateway_settings.gateway_settings.llm_config", return_value={
            "api_key": "test_key",
            "base_url": "https://api.test.com/v1",
            "model": "test-model"
        }):
            with patch("httpx.AsyncClient.post", return_value=mock_resp):
                l1, l2 = await extract_oled_keywords_llm("I see a blurry view of a pale wall with your hand.")
                self.assertEqual(l1, "Seeing Pale Wall")
                self.assertEqual(l2, "Hand In Front")
                self.assertLessEqual(len(l1), 16)
                self.assertLessEqual(len(l2), 16)

    def test_extract_oled_keywords_llm(self):
        import asyncio
        asyncio.run(self._run_llm_summarizer())

if __name__ == "__main__":
    unittest.main()

