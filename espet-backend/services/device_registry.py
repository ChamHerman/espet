"""
Device Registry — hybrid-mode device connections and chat bindings.

Tracks, per device_id:
  - live WebSocket connections (set/cleared by api/cloud_router.py)
  - which Telegram chat_ids are bound to the device

Bindings persist to a JSON file so a gateway restart keeps the routing table.
Thread-safe: touched from the WS handler task, the poller task and REST
endpoints — all inside the same event loop, so a plain lock is sufficient.
"""

from __future__ import annotations

import json
import logging
import threading
from pathlib import Path

logger = logging.getLogger("espet-backend.registry")


class DeviceRegistry:
    def __init__(self, persist_path: str | Path = "data/cloud_bindings.json"):
        self.persist_path = Path(persist_path)
        self._lock = threading.Lock()
        # device_id -> live websocket (or None when offline)
        self.connections: dict[str, object] = {}
        # chat_id (str) -> device_id
        self.chat_to_device: dict[str, str] = {}
        self._load()

    # ------------------------------------------------------------- persistence

    def _load(self) -> None:
        if not self.persist_path.exists():
            return
        try:
            data = json.loads(self.persist_path.read_text())
            with self._lock:
                for chat_id, device_id in data.get("bindings", {}).items():
                    self.chat_to_device[str(chat_id)] = str(device_id)
            logger.info("Loaded %d chat binding(s) from %s",
                        len(data.get("bindings", {})), self.persist_path)
        except Exception as e:  # corrupted file must never kill the gateway
            logger.warning("Could not load bindings from %s: %s",
                           self.persist_path, e)

    def _save_locked(self) -> None:
        try:
            self.persist_path.parent.mkdir(parents=True, exist_ok=True)
            self.persist_path.write_text(json.dumps(
                {"bindings": self.chat_to_device}, indent=2))
        except Exception as e:
            logger.warning("Could not persist bindings to %s: %s",
                           self.persist_path, e)

    # ------------------------------------------------------------- connections

    def register_connection(self, device_id: str, ws) -> None:
        with self._lock:
            old = self.connections.get(device_id)
            self.connections[device_id] = ws
        if old is not None and old is not ws:
            logger.warning("Device %s reconnected — replacing stale connection",
                           device_id)

    def remove_connection(self, device_id: str, ws) -> None:
        with self._lock:
            if self.connections.get(device_id) is ws:
                del self.connections[device_id]

    def is_connected(self, device_id: str) -> bool:
        with self._lock:
            return device_id in self.connections

    def connected_devices(self) -> list[str]:
        with self._lock:
            return list(self.connections.keys())

    def get_connection(self, device_id: str | None = None):
        """Returns (device_id, websocket) for the specified device or the first connected device."""
        with self._lock:
            if device_id and device_id in self.connections:
                return device_id, self.connections[device_id]
            if self.connections:
                did = next(iter(self.connections))
                return did, self.connections[did]
            return None, None

    # ------------------------------------------------------------- bindings

    def bind(self, chat_id: str | int, device_id: str) -> None:
        chat_id = str(chat_id)
        with self._lock:
            prev = self.chat_to_device.get(chat_id)
            self.chat_to_device[chat_id] = str(device_id)
            self._save_locked()
        if prev != str(device_id):
            logger.info("Bound chat %s -> device %s (was %s)",
                        chat_id, device_id, prev)

    def unbind(self, chat_id: str | int) -> bool:
        chat_id = str(chat_id)
        with self._lock:
            if chat_id in self.chat_to_device:
                del self.chat_to_device[chat_id]
                self._save_locked()
                return True
        return False

    def device_for_chat(self, chat_id: str | int) -> str | None:
        with self._lock:
            return self.chat_to_device.get(str(chat_id))

    def bindings(self) -> dict[str, str]:
        with self._lock:
            return dict(self.chat_to_device)

    def chats_for_device(self, device_id: str) -> list[str]:
        did_str = str(device_id)
        with self._lock:
            return [cid for cid, did in self.chat_to_device.items() if did == did_str]

    def primary_chat_for_device(self, device_id: str) -> str | None:
        chats = self.chats_for_device(device_id)
        return chats[0] if chats else None

    def auto_bind_single_device(self, chat_id: str | int) -> str | None:
        """Dev convenience: if exactly one device is connected and the chat is
        unbound, bind them. Returns the device_id or None."""
        with self._lock:
            chat_id = str(chat_id)
            if chat_id in self.chat_to_device:
                return self.chat_to_device[chat_id]
            if len(self.connections) == 1:
                device_id = next(iter(self.connections))
                self.chat_to_device[chat_id] = device_id
                self._save_locked()
                logger.info("Auto-bound chat %s -> device %s (only device online)",
                            chat_id, device_id)
                return device_id
        return None

    def ws_for_chat(self, chat_id: str | int):
        """Live websocket for the device bound to chat_id (None if offline)."""
        device_id = self.device_for_chat(chat_id)
        if device_id is None:
            return None, None
        with self._lock:
            return device_id, self.connections.get(device_id)
