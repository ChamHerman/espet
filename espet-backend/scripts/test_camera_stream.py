#!/usr/bin/env python3
"""Synthetic camera stream smoke test.

Starts no server by itself. Run the backend first, then this script emulates the
ESP32 camera binary WebSocket and verifies status, snapshot, and MJPEG output.
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import json
from urllib.parse import urlencode

import httpx
import websockets


JPEG_1X1 = base64.b64decode(
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAP//////////////////////////////////////////////////////////////////////////////////////2wBDAf//////////////////////////////////////////////////////////////////////////////////////wAARCAABAAEDASIAAhEBAxEB/8QAFQABAQAAAAAAAAAAAAAAAAAAAAX/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/9oADAMBAAIQAxAAAAH/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/9oACAEBAAEFAqf/xAAUEQEAAAAAAAAAAAAAAAAAAAAA/9oACAEDAQE/ASP/xAAUEQEAAAAAAAAAAAAAAAAAAAAA/9oACAECAQE/ASP/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/9oACAEBAAY/Aqf/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/9oACAEBAAE/ISP/2gAMAwEAAgADAAAAEP/EFBQRAQAAAAAAAAAAAAAAAAAAABD/2gAIAQMBAT8QH//EFBQRAQAAAAAAAAAAAAAAAAAAABD/2gAIAQIBAT8QH//EFBABAQAAAAAAAAAAAAAAAAAAARD/2gAIAQEAAT8QH//Z"
)


async def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="http://127.0.0.1:8000")
    parser.add_argument("--key", default="espet_secret_device_key_2026")
    parser.add_argument("--device-id", default="test_camera")
    parser.add_argument("--frames", type=int, default=5)
    args = parser.parse_args()

    ws_base = args.base.replace("http://", "ws://").replace("https://", "wss://").rstrip("/")
    qs = urlencode({"key": args.key, "device_id": args.device_id})
    ws_url = f"{ws_base}/api/camera/ws/device?{qs}"

    async with websockets.connect(ws_url, max_size=2_000_000) as ws:
        for _ in range(args.frames):
            await ws.send(JPEG_1X1)
            await asyncio.sleep(0.05)

    headers = {"Authorization": f"Bearer {args.key}"}
    async with httpx.AsyncClient(base_url=args.base, timeout=10.0) as client:
        status = (await client.get(f"/api/camera/status?device_id={args.device_id}", headers=headers)).json()
        assert status["frame_count"] >= args.frames, status

        snap = await client.get(f"/api/camera/snapshot?device_id={args.device_id}&key={args.key}")
        assert snap.status_code == 200, snap.text
        assert snap.headers.get("content-type", "").startswith("image/jpeg"), snap.headers
        assert snap.content.startswith(b"\xff\xd8"), "snapshot is not JPEG"

        async with client.stream("GET", f"/api/camera/stream?device_id={args.device_id}&key={args.key}&mode=raw&fps=2") as resp:
            assert resp.status_code == 200, resp.status_code
            chunk = await anext(resp.aiter_bytes())
            assert b"Content-Type: image/jpeg" in chunk, chunk[:200]

    print(json.dumps({"ok": True, "status": status}, indent=2))


if __name__ == "__main__":
    asyncio.run(main())
