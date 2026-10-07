"""
ESPET Backend Server — Configuration

Loads settings from .env file and provides typed access via Pydantic.
"""

from pydantic_settings import BaseSettings
from pathlib import Path


class Settings(BaseSettings):
    """Application settings loaded from .env file."""

    # --- Server ---
    host: str = "0.0.0.0"
    port: int = 8000
    device_api_key: str = "change-me-to-a-random-secret"

    # --- Google OAuth 2.0 ---
    google_client_id: str = ""
    google_client_secret: str = ""
    google_redirect_uri: str = "http://localhost:8000/auth/google/callback"

    # Google OAuth scopes needed for Gmail, Calendar, Drive
    google_scopes: list[str] = [
        "https://www.googleapis.com/auth/gmail.modify",
        "https://www.googleapis.com/auth/gmail.readonly",
        "https://www.googleapis.com/auth/gmail.send",
        "https://www.googleapis.com/auth/gmail.compose",
        "https://www.googleapis.com/auth/calendar",
        "https://www.googleapis.com/auth/calendar.events",
        "https://www.googleapis.com/auth/drive.readonly",
        "https://www.googleapis.com/auth/drive.file",
    ]

    # --- Google MCP Server Endpoints (Official) ---
    gmail_mcp_url: str = "https://gmailmcp.googleapis.com/mcp/v1"
    calendar_mcp_url: str = "https://calendarmcp.googleapis.com/mcp/v1"
    drive_mcp_url: str = "https://drivemcp.googleapis.com/mcp/v1"

    # --- Voice pipeline (STT/TTS) ---
    # STT: faster-whisper model size: tiny, base, small, medium...
    voice_stt_model: str = "base"
    # None = auto-detect language; "en"/"zh" forces a language
    voice_language: str | None = None
    # TTS: edge-tts voice (neural, free). zh-CN = Mandarin, zh-HK = Cantonese:
    #   zh-CN-XiaoxiaoNeural (female) / zh-CN-YunxiNeural (male)
    #   zh-HK-HiuMaanNeural / en-US-AriaNeural ...
    voice_tts_voice: str = "zh-CN-XiaoxiaoNeural"

    # --- Database ---
    db_path: str = "db/tokens.db"

    model_config = {
        "env_file": ".env",
        "env_file_encoding": "utf-8",
        "extra": "ignore",
    }


# Singleton instance — import this everywhere
settings = Settings()
