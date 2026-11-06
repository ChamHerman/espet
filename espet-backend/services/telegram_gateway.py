"""
Telegram Gateway — the cloud side of ESPET hybrid mode.

Single poller for the product bot (Telegram allows one getUpdates consumer
per token). Inbound updates are routed to the owning device over its live
WebSocket (see api/cloud_router.py); outbound device frames (message_out /
photo_out) become Telegram API calls.

The HTTP client is injected so tests can point api_base at a mock Telegram
server (see tests/test_cloud_gateway.py).
"""

from __future__ import annotations

import asyncio
import json
import logging
from pathlib import Path

import httpx

logger = logging.getLogger("espet-backend.tg-gateway")


class TelegramGateway:
    def __init__(
        self,
        registry,
        bot_token: str,
        api_base: str = "https://api.telegram.org",
        poll_interval_s: float = 0.0,
        long_poll_s: int = 30,
        offset_path: str | Path = "data/telegram_offset.txt",
        client: httpx.AsyncClient | None = None,
    ):
        self.registry = registry
        self.bot_token = bot_token
        self.api_base = api_base.rstrip("/")
        self.poll_interval_s = poll_interval_s
        self.long_poll_s = long_poll_s
        self.offset_path = Path(offset_path)
        self.client = client or httpx.AsyncClient(timeout=long_poll_s + 15)
        self._offset = self._load_offset()
        self._task: asyncio.Task | None = None
        self._running = False

    # ------------------------------------------------------------------ urls

    def _url(self, method: str) -> str:
        return f"{self.api_base}/bot{self.bot_token}/{method}"

    # ----------------------------------------------------------------- offset

    def _load_offset(self) -> int:
        try:
            return int(self.offset_path.read_text().strip())
        except Exception:
            return 0

    def _save_offset(self, offset: int) -> None:
        self._offset = offset
        try:
            self.offset_path.parent.mkdir(parents=True, exist_ok=True)
            self.offset_path.write_text(str(offset))
        except Exception as e:
            logger.warning("Could not persist Telegram offset: %s", e)

    # ------------------------------------------------------------- poller

    def start(self) -> None:
        if self._running:
            return
        self._running = True
        self._task = asyncio.create_task(self._run())
        logger.info("Telegram gateway poller started (base=%s)", self.api_base)

    async def stop(self) -> None:
        self._running = False
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except (asyncio.CancelledError, Exception):
                pass
            self._task = None

    async def _run(self) -> None:
        backoff = 1.0
        while self._running:
            try:
                got = await self.poll_once()
                backoff = 1.0
                if not got and self.poll_interval_s > 0:
                    await asyncio.sleep(self.poll_interval_s)
            except asyncio.CancelledError:
                raise
            except Exception as e:
                logger.warning("Telegram poll failed: %r — retry in %.1fs",
                               e, backoff)
                await asyncio.sleep(backoff)
                backoff = min(backoff * 2, 30.0)

    async def poll_once(self) -> bool:
        """One getUpdates round. Returns True when updates were handled."""
        params = {"timeout": self.long_poll_s, "limit": 10}
        if self._offset:
            params["offset"] = self._offset
        resp = await self.client.get(self._url("getUpdates"), params=params)
        resp.raise_for_status()
        data = resp.json()
        if not data.get("ok"):
            logger.warning("getUpdates not ok: %s", str(data)[:200])
            return False
        updates = data.get("result", [])
        for update in updates:
            self._save_offset(update["update_id"] + 1)
            await self.handle_update(update)
        return bool(updates)

    # ------------------------------------------------------------- routing

    async def handle_update(self, update: dict) -> None:
        msg = update.get("message") or update.get("edited_message") or {}
        chat = msg.get("chat", {})
        chat_id = chat.get("id")
        text = msg.get("text")
        if chat_id is None:
            return

        device_id = self.registry.device_for_chat(chat_id)
        if device_id is None:
            device_id = self.registry.auto_bind_single_device(chat_id)
        if device_id is None:
            logger.warning("No device bound/connected for chat %s — dropping "
                           "'%.60s'", chat_id, text or "<non-text>")
            return

        frame = json.dumps({
            "type": "message_in",
            "chat_id": str(chat_id),
            "text": text or "",
            "message_id": msg.get("message_id"),
            "from_name": (msg.get("from") or {}).get("first_name", ""),
        })
        ws = self.registry.connections.get(device_id)
        if ws is None:
            logger.warning("Device %s bound to chat %s is offline — message "
                           "dropped", device_id, chat_id)
            return
        try:
            await ws.send_text(frame)
            logger.info("Routed Telegram msg %s (chat %s) -> device %s",
                        msg.get("message_id"), chat_id, device_id)
        except Exception as e:
            logger.warning("Push to device %s failed: %r", device_id, e)

    # ------------------------------------------------------------- outbound

    async def send_text(self, chat_id: str | int, text: str) -> bool:
        resp = await self.client.post(self._url("sendMessage"),
                                      json={"chat_id": str(chat_id), "text": text})
        ok = resp.status_code == 200 and resp.json().get("ok", False)
        if not ok:
            logger.warning("sendMessage to %s failed: HTTP %d %.200s",
                           chat_id, resp.status_code, resp.text)
        return ok

    async def send_photo(self, chat_id: str | int, image: bytes,
                         caption: str = "") -> bool:
        files = {"photo": ("capture.jpg", image, "image/jpeg")}
        data = {"chat_id": str(chat_id)}
        if caption:
            data["caption"] = caption
        resp = await self.client.post(self._url("sendPhoto"), data=data,
                                      files=files)
        ok = resp.status_code == 200 and resp.json().get("ok", False)
        if not ok:
            logger.warning("sendPhoto to %s failed: HTTP %d %.200s",
                           chat_id, resp.status_code, resp.text)
        return ok


# ---------------------------------------------------------------------------
# Module-level singleton (constructed lazily from settings; tests replace it)
# ---------------------------------------------------------------------------
_gateway: TelegramGateway | None = None


def get_gateway() -> TelegramGateway | None:
    return _gateway


def set_gateway(gw: TelegramGateway | None) -> None:
    global _gateway
    _gateway = gw
