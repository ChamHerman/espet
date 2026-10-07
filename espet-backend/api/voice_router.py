"""
Voice Router — Speech-to-Text (STT) and Text-to-Speech (TTS) for the ESP32

The ESP32 never runs AI models: it captures raw 16 kHz mono 16-bit PCM from
the INMP441 and POSTs it here, and fetches WAV audio from here to play on the
MAX98357A. All heavy lifting (Whisper inference, neural TTS) happens on this
server.

Endpoints (device-key auth):
    POST /api/voice/transcribe  raw PCM/WAV body  -> {"text": "..."}
    POST /api/voice/tts         {"text": "..."}   -> audio/wav (16 kHz mono 16-bit)

STT:  faster-whisper (local, free). Model configurable via VOICE_STT_MODEL.
TTS:  edge-tts (Microsoft Edge neural voices, free, no API key) + ffmpeg
      to convert MP3 -> 16 kHz mono 16-bit WAV the I2S TX path can stream.
"""

import logging
import math
import struct
import tempfile
from pathlib import Path

import numpy as np
from fastapi import APIRouter, Depends, HTTPException, Request
from fastapi.responses import Response
from pydantic import BaseModel

from auth.device_auth import verify_device_key
from config import settings

router = APIRouter(prefix="/api/voice", tags=["voice"])
logger = logging.getLogger("espet-backend.voice")

# Lazily-created singletons (Whisper model load takes seconds — do it once)
_stt_model = None

# Sample rate the ESP32 I2S pipeline runs at — keep this contract fixed
ESP_SAMPLE_RATE = 16000


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------
import re as _re
_EMOJI_RE = _re.compile(
    "[\U0001F000-\U0001FAFF\U00002600-\U000027BF\U0001F900-\U0001F9FF\u200d\ufe0f]+",
    flags=_re.UNICODE)


# Speaking cap: ~1.35 KB of WAV per character, so 800 chars ~ 1.1 MB WAV
# (~35 s of speech — anything longer is unreasonable to listen to anyway).
TTS_MAX_CHARS = 800


def sanitize_tts_text(text: str) -> str:
    """Strip emoji/pictographs edge-tts reads badly, then cap length."""
    text = _EMOJI_RE.sub(" ", text).strip()
    if len(text) > TTS_MAX_CHARS:
        text = text[:TTS_MAX_CHARS].rstrip() + "…"
    return text


def pcm_to_wav_bytes(pcm16: np.ndarray, sample_rate: int = ESP_SAMPLE_RATE) -> bytes:
    """Wrap signed 16-bit mono PCM in a minimal 44-byte WAV container."""
    data = pcm16.astype("<i2").tobytes()
    header = struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF", 36 + len(data), b"WAVE",
        b"fmt ", 16, 1, 1, sample_rate, sample_rate * 2, 2, 16,
        b"data", len(data),
    )
    return header + data


def wav_bytes_to_pcm16(wav: bytes) -> np.ndarray:
    """Extract 16-bit mono PCM samples from a WAV byte blob (skips header)."""
    if len(wav) < 44 or wav[:4] != b"RIFF" or wav[8:12] != b"WAVE":
        raise ValueError("not a WAV file")
    # Find the data chunk (handles extra chunks before data)
    idx = 12
    data_off = data_len = None
    while idx + 8 <= len(wav):
        cid = wav[idx:idx + 4]
        clen = struct.unpack("<I", wav[idx + 4:idx + 8])[0]
        if cid == b"data":
            data_off, data_len = idx + 8, min(clen, len(wav) - idx - 8)
            break
        idx += 8 + clen + (clen & 1)
    if data_off is None:
        raise ValueError("WAV data chunk not found")
    raw = wav[data_off:data_off + data_len]
    # 16-bit mono expected; if stereo, drop every second sample
    samples = np.frombuffer(raw[: len(raw) // 2 * 2], dtype="<i2").copy()
    fmt_ch = struct.unpack("<H", wav[22:24])[0] if len(wav) > 24 else 1
    if fmt_ch > 1:
        samples = samples[::fmt_ch]
    return samples


def resample_to_16k(pcm16: np.ndarray, orig_rate: int) -> np.ndarray:
    """Simple linear-interpolation resampler down/up to 16 kHz."""
    if orig_rate == ESP_SAMPLE_RATE or len(pcm16) == 0:
        return pcm16
    n_out = int(math.ceil(len(pcm16) * ESP_SAMPLE_RATE / orig_rate))
    x_in = np.arange(len(pcm16), dtype=np.float64)
    x_out = np.arange(n_out, dtype=np.float64) * orig_rate / ESP_SAMPLE_RATE
    return np.interp(x_out, x_in, pcm16.astype(np.float64)).astype(np.int16)


# ---------------------------------------------------------------------------
# STT
# ---------------------------------------------------------------------------
def _get_stt_model():
    """Load faster-whisper once (first call takes a few seconds)."""
    global _stt_model
    if _stt_model is None:
        from faster_whisper import WhisperModel
        model_name = settings.voice_stt_model
        logger.info("Loading faster-whisper model '%s' (first call only)...", model_name)
        _stt_model = WhisperModel(
            model_name, device="cpu", compute_type="int8", cpu_threads=4
        )
    return _stt_model


def _transcribe_pcm(pcm16: np.ndarray, language: str | None) -> str:
    """Write a temp WAV, run faster-whisper, return the transcript text."""
    if len(pcm16) < ESP_SAMPLE_RATE // 5:  # < 0.2 s of audio
        return ""
    model = _get_stt_model()
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        f.write(pcm_to_wav_bytes(pcm16))
        tmp_path = f.name
    try:
        segments, info = model.transcribe(
            tmp_path,
            language=language or None,
            beam_size=1,           # latency over accuracy for voice assistant use
            vad_filter=True,       # skip silence — big win on noisy captures
        )
        text = " ".join(seg.text.strip() for seg in segments).strip()
        logger.info("STT ok (lang=%s, %.1f s audio): %.80s", info.language, info.duration, text)
        return text
    finally:
        Path(tmp_path).unlink(missing_ok=True)


# ---------------------------------------------------------------------------
# TTS
# ---------------------------------------------------------------------------
async def _tts_to_wav(text: str, voice: str) -> bytes:
    """edge-tts -> MP3 -> (ffmpeg) -> 16 kHz mono 16-bit WAV bytes."""
    import asyncio
    import edge_tts

    with tempfile.NamedTemporaryFile(suffix=".mp3", delete=False) as f:
        mp3_path = f.name
    wav_path = mp3_path.replace(".mp3", ".wav")
    try:
        communicate = edge_tts.Communicate(text, voice)
        await asyncio.wait_for(communicate.save(mp3_path), timeout=30)

        proc = await asyncio.create_subprocess_exec(
            "ffmpeg", "-y", "-loglevel", "error", "-i", mp3_path,
            "-ar", str(ESP_SAMPLE_RATE), "-ac", "1", "-sample_fmt", "s16",
            wav_path,
        )
        try:
            await asyncio.wait_for(proc.wait(), timeout=30)
        except asyncio.TimeoutError:
            proc.kill()
            raise HTTPException(504, "ffmpeg timed out")
        if proc.returncode != 0:
            raise HTTPException(500, "ffmpeg failed to convert TTS audio")

        wav = Path(wav_path).read_bytes()
        logger.info("TTS ok (%.60s) -> %d bytes WAV", text, len(wav))
        return wav
    finally:
        Path(mp3_path).unlink(missing_ok=True)
        Path(wav_path).unlink(missing_ok=True)


# ---------------------------------------------------------------------------
# endpoints
# ---------------------------------------------------------------------------
class TTSRequest(BaseModel):
    text: str
    voice: str | None = None  # optional edge-tts voice override


@router.post("/transcribe", dependencies=[Depends(verify_device_key)])
async def transcribe(request: Request):
    """
    Accept raw 16 kHz mono 16-bit PCM (Content-Type: application/octet-stream,
    the ESP32's native capture format) or a WAV blob. Returns transcript JSON.
    """
    body = await request.body()
    if not body:
        raise HTTPException(400, "empty body")
    if len(body) > 3_000_000:  # ~90 s of PCM — way over the PTT cap
        raise HTTPException(413, "audio too large")

    ctype = (request.headers.get("content-type") or "").lower()
    try:
        raw_rate = int(request.headers.get("x-audio-rate") or ESP_SAMPLE_RATE)
    except ValueError:
        raw_rate = ESP_SAMPLE_RATE
    rate = raw_rate if raw_rate > 0 else ESP_SAMPLE_RATE
    try:
        if "wav" in ctype or body[:4] == b"RIFF":
            pcm = wav_bytes_to_pcm16(body)
        else:
            raw = np.frombuffer(body[: len(body) // 2 * 2], dtype="<i2").copy()
            pcm = resample_to_16k(raw, rate) if rate != ESP_SAMPLE_RATE else raw
        text = await _run_stt(pcm)
    except HTTPException:
        raise
    except Exception as e:
        logger.exception("STT failed")
        raise HTTPException(500, f"transcription failed: {e}")

    return {"text": text}


async def _run_stt(pcm: np.ndarray) -> str:
    """Offload the blocking Whisper call to a worker thread."""
    import asyncio
    return await asyncio.to_thread(_transcribe_pcm, pcm, settings.voice_language)


@router.post("/tts", dependencies=[Depends(verify_device_key)])
async def tts(req: TTSRequest):
    """Synthesize speech and return 16 kHz mono 16-bit WAV (ESP32-ready)."""
    text = sanitize_tts_text(req.text.strip())
    if not text:
        raise HTTPException(400, "text is empty after sanitization (emoji-only reply?)")
    if len(text) > 2000:
        raise HTTPException(413, "text too long for voice playback (max 2000 chars)")

    voice = req.voice or settings.voice_tts_voice
    try:
        wav = await _tts_to_wav(text, voice)
    except HTTPException:
        raise
    except Exception as e:
        logger.exception("TTS failed")
        raise HTTPException(500, f"tts failed: {e}")

    return Response(content=wav, media_type="audio/wav")
