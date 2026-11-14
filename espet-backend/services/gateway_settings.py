"""
Gateway runtime settings — Telegram bot + LLM credentials for hybrid mode.

Persisted to data/gateway_config.json and editable from the dashboard
(REST: /api/cloud/gateway-config). Values missing from the file fall back to
the environment (config.Settings: TELEGRAM_BOT_TOKEN / LLM_* if present), so
.env works as the bootstrap default and the dashboard overrides at runtime.

Changes take effect without restarting the backend: cloud_router rebuilds
the Telegram poller when the token/enabled flag changes, and the LLM relay
reads the current values on every request.
"""

from __future__ import annotations

import json
import logging
import threading
from pathlib import Path

from config import settings

logger = logging.getLogger("espet-backend.gw-settings")

_KEYS = (
    "telegram_bot_token",
    "cloud_gateway_enabled",
    "telegram_poll_interval_s",
    "llm_api_key",
    "llm_base_url",
    "llm_model",
    "vision_model",
)


class GatewaySettings:
    def __init__(self, persist_path: str | Path = "data/gateway_config.json"):
        self.persist_path = Path(persist_path)
        self._lock = threading.Lock()
        self._values: dict = {}
        self._load()

    # ------------------------------------------------------------------ load

    def _load(self) -> None:
        values = {
            "telegram_bot_token": settings.telegram_bot_token,
            "cloud_gateway_enabled": settings.cloud_gateway_enabled,
            "telegram_poll_interval_s": settings.telegram_poll_interval_s,
            "llm_api_key": getattr(settings, "llm_api_key", ""),
            "llm_base_url": getattr(settings, "llm_base_url", "https://api.openai.com/v1"),
            "llm_model": getattr(settings, "llm_model", ""),
            "vision_model": getattr(settings, "vision_model", "sensenova/sensenova-6.8-flash-lite"),
        }
        try:
            if self.persist_path.exists():
                stored = json.loads(self.persist_path.read_text())
                for k in _KEYS:
                    if k in stored and stored[k] not in (None, ""):
                        values[k] = stored[k]
                logger.info("Loaded gateway settings from %s", self.persist_path)
        except Exception as e:
            logger.warning("Could not load gateway settings (%s): %s",
                           self.persist_path, e)
        with self._lock:
            self._values = values

    # ------------------------------------------------------------------ api

    def get(self) -> dict:
        with self._lock:
            return dict(self._values)

    def update(self, partial: dict) -> dict:
        """Merge a partial dict (only known keys) and persist. Returns the
        fresh snapshot."""
        with self._lock:
            for k in _KEYS:
                if k in partial and partial[k] is not None:
                    self._values[k] = partial[k]
            try:
                self.persist_path.parent.mkdir(parents=True, exist_ok=True)
                self.persist_path.write_text(
                    json.dumps(self._values, indent=2))
            except Exception as e:
                logger.warning("Could not persist gateway settings: %s", e)
            return dict(self._values)

    # Convenience accessors used on the hot paths
    def telegram_token(self) -> str:
        return str(self.get().get("telegram_bot_token") or "")

    def llm_config(self) -> dict:
        snap = self.get()
        return {"api_key": snap.get("llm_api_key") or "",
                "base_url": snap.get("llm_base_url") or "",
                "model": snap.get("llm_model") or ""}


# Singleton
gateway_settings = GatewaySettings()
