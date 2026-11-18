import asyncio
import base64
import unittest
from unittest.mock import AsyncMock, patch, MagicMock

import httpx
from fastapi import Request

from api.camera_router import _clean_oled_line, vision_query


class TestCameraVision(unittest.TestCase):
    def test_clean_oled_line(self):
        self.assertEqual(_clean_oled_line(""), "")
        self.assertEqual(_clean_oled_line("Hello World!"), "Hello World!")
        self.assertEqual(_clean_oled_line("This line is way too long for sixteen chars"), "This line is way")
        self.assertEqual(_clean_oled_line("你好世界 Hello"), "Hello")
        self.assertEqual(_clean_oled_line("Line 1\nwith newline"), "Line 1 with newl")

    async def _run_vision_query_json(self):
        fake_jpeg = b"\xff\xd8\xff\xe0" + b"\x00" * 20
        mock_request = MagicMock(spec=Request)
        mock_request.body = AsyncMock(return_value=fake_jpeg)
        mock_request.headers = {}

        mock_gw = MagicMock()
        mock_gw.send_photo = AsyncMock(return_value=True)
        mock_gw.send_text = AsyncMock(return_value=True)

        json_payload = {
            "choices": [
                {
                    "message": {
                        "content": '{"oled": ["Seeing pale wall", "Hand in front"], "reply": "I see a pale wall."}'
                    }
                }
            ]
        }
        mock_http_resp = httpx.Response(
            200,
            json=json_payload,
            request=httpx.Request("POST", "https://api.test.com/v1/chat/completions"),
        )

        with patch("api.camera_router.get_gateway", return_value=mock_gw):
            with patch("api.camera_router.registry.primary_chat_for_device", return_value="123456"):
                with patch("httpx.AsyncClient.post", return_value=mock_http_resp):
                    res = await vision_query(mock_request, query="What is this?", device_id="ESP-112233")
                    self.assertTrue(res["ok"])
                    self.assertEqual(res["reply"], "I see a pale wall.")
                    self.assertEqual(res["oled_l1"], "Seeing pale wall")
                    self.assertEqual(res["oled_l2"], "Hand in front")

                    # Verify Telegram gateway calls
                    mock_gw.send_photo.assert_awaited_once()
                    mock_gw.send_text.assert_awaited_once_with("123456", "🤖 ESPET: I see a pale wall.")

    def test_vision_query_json(self):
        asyncio.run(self._run_vision_query_json())

    async def _run_vision_query_markdown_codeblock(self):
        fake_jpeg = b"\xff\xd8\xff\xe0" + b"\x00" * 20
        mock_request = MagicMock(spec=Request)
        mock_request.body = AsyncMock(return_value=fake_jpeg)
        mock_request.headers = {}

        mock_gw = MagicMock()
        mock_gw.send_photo = AsyncMock(return_value=True)
        mock_gw.send_text = AsyncMock(return_value=True)

        md_content = '```json\n{"oled": ["Pale Surface", "Sticker Centered"], "reply": "I see a pale surface with a sticker."}\n```'
        json_payload = {
            "choices": [
                {
                    "message": {
                        "content": md_content
                    }
                }
            ]
        }
        mock_http_resp = httpx.Response(
            200,
            json=json_payload,
            request=httpx.Request("POST", "https://api.test.com/v1/chat/completions"),
        )

        with patch("api.camera_router.get_gateway", return_value=mock_gw):
            with patch("api.camera_router.registry.primary_chat_for_device", return_value="123456"):
                with patch("httpx.AsyncClient.post", return_value=mock_http_resp):
                    res = await vision_query(mock_request, query="What is this?", device_id="ESP-112233")
                    self.assertTrue(res["ok"])
                    self.assertEqual(res["reply"], "I see a pale surface with a sticker.")
                    self.assertEqual(res["oled_l1"], "Pale Surface")
                    self.assertEqual(res["oled_l2"], "Sticker Centered")
                    mock_gw.send_text.assert_awaited_once_with("123456", "🤖 ESPET: I see a pale surface with a sticker.")

    def test_vision_query_markdown_codeblock(self):
        asyncio.run(self._run_vision_query_markdown_codeblock())


if __name__ == "__main__":
    unittest.main()
