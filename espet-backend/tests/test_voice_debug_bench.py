"""
Regression & integration tests for the /stt Voice & Speaker Debug Bench.

Verifies:
  1. /stt debug webpage serves cleanly with all STT, TTS, speaker, and regression hooks.
  2. Auth enforcement on /api/voice/debug/* and /api/cloud/tts-say.
  3. /api/voice/debug/config, /api/voice/debug/voices, /api/voice/debug/transcribe,
     /api/voice/debug/tts (English + Chinese), and /api/voice/debug/regression.
  4. /api/cloud/tts-say <-> /api/cloud/ws/device tts_say / tts_say_status roundtrip.
  5. Existing production endpoints (/api/voice/tts, /api/voice/transcribe) remain unbroken
     and data/last_capture.wav is not overwritten by debug/regression runs.

Run:
    python tests/test_voice_debug_bench.py
or:
    python -m pytest tests/test_voice_debug_bench.py -v
"""

from __future__ import annotations

import asyncio
import json
import socket
import struct
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

import httpx
import numpy as np
import uvicorn
from fastapi import FastAPI
from websockets.asyncio.client import connect

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from fastapi.testclient import TestClient
import main
from api.cloud_router import registry, router as cloud_router
from api.voice_router import ESP_SAMPLE_RATE, pcm_to_wav_bytes, wav_bytes_to_pcm16


class TestVoiceDebugBench(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.client = TestClient(main.app)
        cls.auth_headers = {"Authorization": f"Bearer {main.settings.device_api_key}"}
        cls.last_cap_path = Path("data/last_capture.wav")
        cls._orig_last_cap = cls.last_cap_path.read_bytes() if cls.last_cap_path.exists() else None

    @classmethod
    def tearDownClass(cls):
        if cls._orig_last_cap is not None:
            cls.last_cap_path.parent.mkdir(parents=True, exist_ok=True)
            cls.last_cap_path.write_bytes(cls._orig_last_cap)

    def _assert_last_capture_unmodified(self):
        if self._orig_last_cap is None:
            self.assertFalse(self.last_cap_path.exists(), "Debug/regression should not create last_capture.wav")
        else:
            self.assertEqual(self.last_cap_path.read_bytes(), self._orig_last_cap)

    def test_stt_page_served(self):
        resp = self.client.get("/stt")
        self.assertEqual(resp.status_code, 200)
        self.assertIn("text/html", resp.headers.get("content-type", ""))
        for marker in (
            "btn-hw-rec",
            "btn-pc-rec",
            "btn-tts-browser",
            "btn-tts-esp32",
            "btn-run-reg",
            "/api/voice/debug/transcribe",
            "/api/voice/debug/tts",
            "/api/voice/debug/regression",
            "/api/cloud/tts-say",
        ):
            self.assertIn(marker, resp.text)

    def test_debug_endpoints_require_auth(self):
        for method, path in (
            ("GET", "/api/voice/debug/config"),
            ("GET", "/api/voice/debug/voices"),
            ("POST", "/api/voice/debug/transcribe"),
            ("POST", "/api/voice/debug/tts"),
            ("POST", "/api/voice/debug/regression"),
            ("POST", "/api/cloud/tts-say"),
        ):
            resp = self.client.request(method, path)
            self.assertIn(resp.status_code, (401, 403), f"{method} {path} returned {resp.status_code}")

    def test_debug_config_and_voices(self):
        cfg = self.client.get("/api/voice/debug/config", headers=self.auth_headers)
        self.assertEqual(cfg.status_code, 200)
        cdata = cfg.json()
        for key in ("stt_model", "voice_hw_rate_8k", "voice_min_rms", "voice_tts_voice", "wake_words", "last_capture"):
            self.assertIn(key, cdata)

        voices = self.client.get("/api/voice/debug/voices", headers=self.auth_headers)
        self.assertEqual(voices.status_code, 200)
        vdata = voices.json()
        self.assertIn("default_voice", vdata)
        self.assertGreaterEqual(len(vdata.get("voices", [])), 4)

    def test_debug_transcribe_silence_and_bypass(self):
        silent_pcm = np.zeros(ESP_SAMPLE_RATE, dtype=np.int16)
        wav = pcm_to_wav_bytes(silent_pcm, ESP_SAMPLE_RATE)

        # 1. Default RMS gate skips silent WAV immediately
        r1 = self.client.post(
            "/api/voice/debug/transcribe?skip_pitch_fix=true",
            content=wav,
            headers={**self.auth_headers, "Content-Type": "audio/wav"},
        )
        self.assertEqual(r1.status_code, 200)
        d1 = r1.json()
        self.assertTrue(d1["ok"])
        self.assertFalse(d1["wake_matched"])
        self.assertIn("rms_below_threshold", d1["skipped_reason"])

        # 2. Bypass RMS gate reaches Whisper and returns confidence metrics without modifying last_capture.wav
        rng = np.random.default_rng(7)
        noise = rng.normal(0, 120, ESP_SAMPLE_RATE).astype(np.int16)
        noise_wav = pcm_to_wav_bytes(noise, ESP_SAMPLE_RATE)
        r2 = self.client.post(
            "/api/voice/debug/transcribe?skip_pitch_fix=true&skip_rms_gate=true&language=en",
            content=noise_wav,
            headers={**self.auth_headers, "Content-Type": "audio/wav"},
        )
        self.assertEqual(r2.status_code, 200)
        d2 = r2.json()
        self.assertTrue(d2["ok"])
        self.assertEqual(d2["skipped_reason"], "")
        self.assertIn("avg_logprob", d2)
        self.assertIn("no_speech_prob", d2)
        self.assertIn("latency_ms", d2)
        self._assert_last_capture_unmodified()

    def test_debug_tts_en_and_zh_headers(self):
        for text, voice in (
            ("Speaker hardware check", "en-US-AriaNeural"),
            ("你好，硬件测试。", "zh-CN-XiaoxiaoNeural"),
        ):
            resp = self.client.post(
                "/api/voice/debug/tts",
                json={"text": text, "voice": voice},
                headers=self.auth_headers,
            )
            self.assertEqual(resp.status_code, 200)
            self.assertEqual(resp.headers.get("content-type"), "audio/wav")
            self.assertEqual(resp.headers.get("x-wav-sample-rate"), "16000")
            self.assertEqual(resp.headers.get("x-wav-channels"), "1")
            self.assertEqual(resp.headers.get("x-wav-bits"), "16")
            self.assertIn("x-tts-latency-ms", resp.headers)
            pcm = wav_bytes_to_pcm16(resp.content)
            self.assertGreater(len(pcm), 1600)

    def test_debug_regression_suite_and_loopback(self):
        resp = self.client.post(
            "/api/voice/debug/regression",
            json={"include_tts": True, "include_loopback": True},
            headers=self.auth_headers,
        )
        self.assertEqual(resp.status_code, 200)
        data = resp.json()
        self.assertTrue(data["ok"], f"Regression failures: {data}")
        self.assertEqual(data["failed"], 0)
        self.assertGreaterEqual(data["passed"], 11)
        self._assert_last_capture_unmodified()

    def test_cloud_tts_say_ws_roundtrip(self):
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
        s.close()

        app = FastAPI()
        app.include_router(cloud_router)
        server = uvicorn.Server(uvicorn.Config(app, host="127.0.0.1", port=port, log_level="error"))
        srv_thread = threading.Thread(target=server.run, daemon=True)
        srv_thread.start()

        deadline = time.time() + 5.0
        while time.time() < deadline:
            try:
                socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
                break
            except OSError:
                time.sleep(0.05)

        orig_persist = registry.persist_path
        orig_devices = registry.devices_path
        tmp_dir = tempfile.mkdtemp(prefix="espet-voice-ws-test-")
        registry.persist_path = Path(tmp_dir) / "cloud_bindings.json"
        registry.devices_path = Path(tmp_dir) / "devices.json"
        device_id = "ESP-DEBUGTEST01"

        async def _run_roundtrip():
            async with connect(f"ws://127.0.0.1:{port}/api/cloud/ws/device?key={main.settings.device_api_key}") as ws:
                await ws.send(json.dumps({"type": "hello", "device_id": device_id, "fw": "test"}))
                ack = json.loads(await asyncio.wait_for(ws.recv(), 5))
                self.assertEqual(ack["type"], "hello_ack")

                async with httpx.AsyncClient(base_url=f"http://127.0.0.1:{port}", timeout=5) as client:
                    post_task = asyncio.create_task(
                        client.post(
                            "/api/cloud/tts-say",
                            json={
                                "device_id": device_id,
                                "text": "Speaker bench hello",
                                "display_title": "Bench Test",
                                "display_body": "OK",
                            },
                            headers=self.auth_headers,
                        )
                    )
                    frame = json.loads(await asyncio.wait_for(ws.recv(), 5))
                    self.assertEqual(frame["type"], "tts_say")
                    self.assertEqual(frame["text"], "Speaker bench hello")
                    self.assertEqual(frame["display_title"], "Bench Test")
                    await ws.send(json.dumps({"type": "tts_say_status", "ok": True}))
                    resp = await post_task
                    self.assertEqual(resp.status_code, 200)
                    rdata = resp.json()
                    self.assertTrue(rdata.get("ok"), rdata)
                    self.assertEqual(rdata.get("device_id"), device_id)

        try:
            asyncio.run(_run_roundtrip())
        finally:
            server.should_exit = True
            srv_thread.join(timeout=3)
            registry.connections.pop(device_id, None)
            registry.persist_path = orig_persist
            registry.devices_path = orig_devices

    def test_production_voice_contracts_unbroken(self):
        # 1. Production /api/voice/tts
        tts_resp = self.client.post(
            "/api/voice/tts",
            json={"text": "Contract check"},
            headers=self.auth_headers,
        )
        self.assertEqual(tts_resp.status_code, 200)
        self.assertEqual(tts_resp.headers.get("content-type"), "audio/wav")
        self.assertEqual(tts_resp.content[:4], b"RIFF")
        self.assertIn("x-oled-line1", tts_resp.headers)
        self.assertIn("x-oled-line2", tts_resp.headers)

        # 2. Production /api/voice/transcribe (silence)
        silent_pcm = np.zeros(ESP_SAMPLE_RATE, dtype=np.int16).tobytes()
        stt_resp = self.client.post(
            "/api/voice/transcribe",
            content=silent_pcm,
            headers={
                **self.auth_headers,
                "Content-Type": "application/octet-stream",
                "X-Audio-Rate": "16000",
            },
        )
        self.assertEqual(stt_resp.status_code, 200)
        sdata = stt_resp.json()
        self.assertEqual(set(sdata.keys()), {"text", "raw_text", "wake_matched", "wake_word"})
        self.assertFalse(sdata["wake_matched"])


if __name__ == "__main__":
    unittest.main()
