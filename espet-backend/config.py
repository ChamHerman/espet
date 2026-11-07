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
    # STT device: "auto", "cuda", or "cpu"
    voice_stt_device: str = "auto"
    # STT compute type: "auto", "float16", "int8", "int8_float16"
    voice_stt_compute_type: str = "auto"
    # STT beam size: 1 for fast greedy decoding, >1 for beam search
    voice_stt_beam_size: int = 1
    # None = auto-detect within allowed languages; "en"/"zh" forces a single language
    voice_language: str | None = None
    # Restrict Whisper auto-detection to these languages (comma-separated, e.g. "en,zh")
    voice_allowed_languages: str = "en,zh"
    # TTS: edge-tts voice (neural, free). zh-CN = Mandarin, zh-HK = Cantonese:
    #   zh-CN-XiaoxiaoNeural (female) / zh-CN-YunxiNeural (male)
    #   zh-HK-HiuMaanNeural / en-US-AriaNeural ...
    voice_tts_voice: str = "zh-CN-XiaoxiaoNeural"
    # Comma-separated wake words checked against transcripts (case-insensitive)
    voice_wake_words: str = "mimi,hey mimi,hi mimi,ok mimi,expert,hey expert,hi expert,espet,hey espet,hi esp,米米,咪咪,小米"
    # Whisper initial_prompt. Leave EMPTY unless you know what you are doing:
    # putting the wake words here biases the decoder and was verified to turn
    # room noise / speaker echo into exactly the wake word (endless self-wake).
    voice_stt_prompt: str = ""
    # Reject wake matches below this confidence. faster-whisper reports
    # avg_logprob per segment; genuine wake utterances measure > -1.0 on this
    # pipeline while noise/echo hallucinations sit lower.
    voice_min_avg_logprob: float = -1.1
    # Reject wake matches when Whisper itself says "probably not speech".
    # Noise hallucinations measured 0.79~0.91; real speech stays < 0.6.
    voice_max_no_speech_prob: float = 0.75
    # Skip STT entirely when the capture is near-silent (RMS below this).
    voice_min_rms: float = 250

    # --- Database ---
    db_path: str = "db/tokens.db"

    # --- Hybrid mode cloud gateway (Telegram & LLM relay for the device) ---
    # When enabled, the backend polls Telegram for the device (single poller)
    # and routes messages over the device's persistent WebSocket /ws/device.
    # The device sets MIMI_CLOUD_LINK_ENABLE=1 and stops polling on its own.
    cloud_gateway_enabled: bool = False
    telegram_bot_token: str = ""
    telegram_poll_interval_s: float = 0.0
    # Overridable for tests (mock Telegram server)
    telegram_api_base: str = "https://api.telegram.org"

    # Server-side LLM relay (hybrid mode: device holds no API keys)
    llm_api_key: str = ""
    llm_base_url: str = "https://api.openai.com/v1"
    llm_model: str = ""

    model_config = {
        "env_file": ".env",
        "env_file_encoding": "utf-8",
        "extra": "ignore",
    }


# Singleton instance — import this everywhere
settings = Settings()
