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

import asyncio
from collections import deque
import json
import logging
import math
import os
import struct
import sys
import tempfile
import threading
import time
import uuid
from pathlib import Path
import subprocess
import numpy as np
from fastapi import APIRouter, Depends, HTTPException, Query, Request
from fastapi.responses import Response
from pydantic import BaseModel

from auth.device_auth import verify_device_key
from config import settings
from services.oled_summarizer import extract_oled_keywords, extract_oled_keywords_llm

router = APIRouter(prefix="/api/voice", tags=["voice"])
logger = logging.getLogger("espet-backend.voice")

# Lazily-created singletons (Whisper model load takes seconds — do it once)
_stt_model = None

# Sample rate the ESP32 I2S pipeline runs at — keep this contract fixed
ESP_SAMPLE_RATE = 16000

# Bounded atomic capture ring so /stt always plays & inspects the exact WAV
# produced by a given transcribe call, immune to concurrent VAD overwrites.
_MAX_DEBUG_CAPTURES = 12
_capture_lock = threading.Lock()
_capture_ring: deque[dict] = deque(maxlen=_MAX_DEBUG_CAPTURES)
_capture_by_id: dict[str, dict] = {}
_latest_capture_meta: dict | None = None


def _atomic_write_bytes(target: Path, data: bytes) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    tmp = target.with_name(f".{target.name}.{os.getpid()}.{threading.get_ident()}.tmp")
    try:
        tmp.write_bytes(data)
        os.replace(tmp, target)
    finally:
        if tmp.exists():
            tmp.unlink(missing_ok=True)


def _store_debug_capture(wav_bytes: bytes, meta: dict, write_last_capture: bool = True) -> dict:
    """Save capture WAV in bounded memory ring and optionally update data/last_capture.wav atomically."""
    global _latest_capture_meta
    cid = uuid.uuid4().hex[:12]
    full_meta = {
        **meta,
        "capture_id": cid,
        "audio_url": f"/api/voice/captures/{cid}",
        "ts": time.time(),
    }
    entry = {"capture_id": cid, "wav_bytes": wav_bytes, "meta": full_meta}
    with _capture_lock:
        if len(_capture_ring) == _capture_ring.maxlen:
            evicted = _capture_ring.popleft()
            _capture_by_id.pop(evicted["capture_id"], None)
        _capture_ring.append(entry)
        _capture_by_id[cid] = entry
        _latest_capture_meta = full_meta

    if write_last_capture:
        try:
            _atomic_write_bytes(Path("data/last_capture.wav"), wav_bytes)
        except Exception:
            pass
    return full_meta


def get_latest_capture_meta() -> dict | None:
    with _capture_lock:
        return dict(_latest_capture_meta) if _latest_capture_meta else None


def get_capture_wav(capture_id: str) -> bytes | None:
    with _capture_lock:
        entry = _capture_by_id.get(capture_id)
        return entry["wav_bytes"] if entry else None


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


def _biquad_notch(signal: np.ndarray, f0: float, bw: float = 8.0, fs: float = float(ESP_SAMPLE_RATE)) -> np.ndarray:
    """Narrow IIR biquad notch filter: completely eliminates f0 with zero comb artifacts."""
    w0 = 2.0 * np.pi * f0 / fs
    r = 1.0 - (np.pi * bw / fs)
    b0 = 1.0
    b1 = -2.0 * np.cos(w0)
    b2 = 1.0
    a1 = -2.0 * r * np.cos(w0)
    a2 = r * r
    gain_dc = (1.0 + b1 + 1.0) / (1.0 + a1 + a2)
    b0 /= gain_dc
    b1 /= gain_dc
    b2 /= gain_dc

    y = np.empty_like(signal)
    x1, x2 = 0.0, 0.0
    y1, y2 = 0.0, 0.0
    for i in range(len(signal)):
        x0 = signal[i]
        out = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1 = x1, x0
        y2, y1 = y1, out
        y[i] = out
    return y


def denoise_and_boost_audio(pcm16: np.ndarray, sample_rate: int = ESP_SAMPLE_RATE) -> np.ndarray:
    """
    Gentle, artifact-free audio conditioning:
    1. Removes DC bias (zero-mean).
    2. Applies studio 80 Hz low-cut filter to remove sub-audible rumble without affecting voice pitch.
    3. Narrow 3-band biquad notch filter at 169.2 Hz, 338.3 Hz, and 507.5 Hz to extinguish
       the breadboard OLED charge-pump hum without affecting voice formants or pitch.
    4. Safe, gentle voice scaling so speech is clearly audible without robotic coloration.
    """
    if len(pcm16) < sample_rate // 10:
        return pcm16

    data = pcm16.astype(np.float64)
    # 1. Strip DC offset cleanly
    data = data - np.mean(data)

    # 2. Gentle 1st-order high-pass filter (fc ≈ 80 Hz) — completely linear and artifact-free
    # R = e^(-2*pi*fc/fs) ≈ 0.969
    R = 0.969
    y = np.empty_like(data)
    y[0] = data[0]
    for i in range(1, len(data)):
        y[i] = data[i] - data[i - 1] + R * y[i - 1]

    # 3. Narrow biquad notch filters at 169.2 Hz, 338.3 Hz, 507.5 Hz (OLED charge pump ripple)
    y = _biquad_notch(y, 169.2, bw=8.0, fs=float(sample_rate))
    y = _biquad_notch(y, 338.3, bw=8.0, fs=float(sample_rate))
    y = _biquad_notch(y, 507.5, bw=8.0, fs=float(sample_rate))

    # 4. Safe, gentle voice scaling (target ~20,000 counts, max 3.5x boost)
    peak = np.max(np.abs(y))
    if peak > 200.0:
        gain = min(20000.0 / peak, 3.5)
        y = y * gain

    return np.clip(y, -32768, 32767).astype(np.int16)


def resample_to_16k(pcm16: np.ndarray, orig_rate: int) -> np.ndarray:
    """Simple linear-interpolation resampler down/up to 16 kHz."""
    if orig_rate == ESP_SAMPLE_RATE or len(pcm16) == 0:
        return pcm16
    n_out = int(math.ceil(len(pcm16) * ESP_SAMPLE_RATE / orig_rate))
    x_in = np.arange(len(pcm16), dtype=np.float64)
    x_out = np.arange(n_out, dtype=np.float64) * orig_rate / ESP_SAMPLE_RATE
    return np.interp(x_out, x_in, pcm16.astype(np.float64)).astype(np.int16)


def detect_pitch_f0(data: np.ndarray, fs: int = ESP_SAMPLE_RATE) -> float:
    """Measure median fundamental pitch F0 across voiced segments using autocorrelation."""
    if len(data) < fs // 4:
        return 0.0
    win = int(fs * 0.04)
    hop = int(fs * 0.01)
    pitches = []
    for i in range(0, len(data) - win, hop):
        seg = data[i:i + win].astype(np.float64)
        seg = seg - np.mean(seg)
        rms = np.sqrt(np.mean(seg**2))
        if rms < 1500:
            continue
        corr = np.correlate(seg, seg, mode="full")[win - 1:]
        min_lag = int(fs / 500)
        max_lag = int(fs / 70)
        peak_lag = min_lag + int(np.argmax(corr[min_lag:max_lag]))
        f0 = fs / peak_lag
        if corr[peak_lag] > 0.45 * corr[0]:
            pitches.append(f0)
    return float(np.median(pitches)) if len(pitches) >= 3 else 0.0


def check_and_fix_pitch(pcm16: np.ndarray, sample_rate: int = ESP_SAMPLE_RATE) -> tuple[np.ndarray, bool]:
    """
    Upsample legacy 8 kHz half-rate ESP32 captures to 16 kHz when voice_hw_rate_8k=True.
    Full-duplex firmware (I2S_STD_SLOT_BOTH) captures genuine 16 kHz directly, so
    when voice_hw_rate_8k=False we pass PCM through untouched to avoid misclassifying
    normal higher-pitched voices (>170 Hz) as 8 kHz.
    """
    if getattr(settings, "voice_hw_rate_8k", False):
        logger.info(
            "Restoring normal 1x tempo and pitch (resampling 8k -> 16k, %d -> %d samples)",
            len(pcm16), len(pcm16) * 2
        )
        corrected = resample_to_16k(pcm16, 8000)
        return corrected, True

    return pcm16, False


# ---------------------------------------------------------------------------
# STT
# ---------------------------------------------------------------------------
def _setup_windows_cuda_dlls() -> None:
    """Register nvidia-cublas and nvidia-cudnn directories for Windows Python 3.8+."""
    if sys.platform != "win32":
        return
    import site
    search_dirs = []
    try:
        search_dirs.extend(site.getsitepackages())
    except Exception:
        pass
    user_site = getattr(site, "getusersitepackages", lambda: None)()
    if user_site:
        search_dirs.append(user_site)
    for base in search_dirs:
        for pkg in ("cublas", "cudnn"):
            bin_dir = os.path.join(base, "nvidia", pkg, "bin")
            if os.path.isdir(bin_dir):
                try:
                    os.add_dll_directory(bin_dir)
                except Exception:
                    pass


def _get_stt_model():
    """Load faster-whisper with device auto-detection and CPU fallback."""
    global _stt_model
    if _stt_model is not None:
        return _stt_model

    from faster_whisper import WhisperModel
    import ctranslate2

    _setup_windows_cuda_dlls()

    model_name = settings.voice_stt_model
    pref_device = (settings.voice_stt_device or "auto").lower()
    pref_compute = (settings.voice_stt_compute_type or "auto").lower()
    cpu_threads = min(4, os.cpu_count() or 4)

    cuda_available = False
    try:
        cuda_available = ctranslate2.get_cuda_device_count() > 0
    except Exception:
        pass

    if pref_device in ("auto", "cuda") and cuda_available:
        compute_type = "float16" if pref_compute == "auto" else pref_compute
        try:
            logger.info("Attempting faster-whisper '%s' on CUDA (%s)...", model_name, compute_type)
            candidate = WhisperModel(
                model_name,
                device="cuda",
                compute_type=compute_type,
            )
            # Eagerly probe CUDA execution to catch missing cublas64_12.dll or cudnn DLLs
            dummy_probe = np.zeros(1600, dtype=np.float32)
            list(candidate.transcribe(dummy_probe, max_new_tokens=1)[0])
            _stt_model = candidate
            logger.info("faster-whisper '%s' loaded and verified on CUDA GPU successfully.", model_name)
            return _stt_model
        except Exception as e:
            logger.warning(
                "CUDA initialization/probe failed for faster-whisper (%s). Falling back to CPU: %s",
                e,
                "For Windows NVIDIA GPU, run: pip install nvidia-cublas-cu12 nvidia-cudnn-cu12" if sys.platform == "win32" else ""
            )
    compute_type = "int8" if pref_compute == "auto" else pref_compute
    logger.info("Loading faster-whisper model '%s' on CPU (%s, threads=%d)...", model_name, compute_type, cpu_threads)
    _stt_model = WhisperModel(
        model_name,
        device="cpu",
        compute_type=compute_type,
        cpu_threads=cpu_threads,
    )
    logger.info("faster-whisper '%s' loaded on CPU successfully.", model_name)
    return _stt_model


def _transcribe_pcm_impl(
    pcm16: np.ndarray,
    language: str | None,
    *,
    apply_denoise: bool = True,
    save_capture: bool = True,
) -> dict:
    """Internal Whisper runner returning transcript + acoustic/confidence diagnostics."""
    if len(pcm16) < ESP_SAMPLE_RATE // 5:  # < 0.2 s of audio
        return {
            "text": "",
            "avg_logprob": 0.0,
            "no_speech_prob": 1.0,
            "detected_language": "",
            "language_prob": 0.0,
            "duration_s": round(len(pcm16) / ESP_SAMPLE_RATE, 3),
            "rms_processed": float(np.sqrt(np.mean(pcm16.astype(np.float64) ** 2))) if len(pcm16) else 0.0,
        }

    # Clean electrical buzz (OLED charge pump 169 Hz harmonics) and normalize human voice
    if apply_denoise:
        pcm16 = denoise_and_boost_audio(pcm16)
    rms_processed = float(np.sqrt(np.mean(pcm16.astype(np.float64) ** 2))) if len(pcm16) else 0.0

    model = _get_stt_model()
    wav_bytes = pcm_to_wav_bytes(pcm16)
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        f.write(wav_bytes)
        tmp_path = f.name
    # Save last audio capture atomically (never clobber during synthetic regression runs)
    if save_capture:
        try:
            _atomic_write_bytes(Path("data/last_capture.wav"), wav_bytes)
        except Exception:
            pass
    allowed_langs = [
        l.strip().lower() for l in (settings.voice_allowed_languages or "").split(",") if l.strip()
    ]
    target_lang = language if language else (allowed_langs[0] if len(allowed_langs) == 1 else None)

    try:
        beam_size = max(1, settings.voice_stt_beam_size)
        try:
            segments, info = model.transcribe(
                tmp_path,
                language=target_lang,
                beam_size=beam_size,
                best_of=beam_size,
                repetition_penalty=1.2,
                no_repeat_ngram_size=3,
                max_new_tokens=96,
                vad_filter=True,
                vad_parameters=dict(min_silence_duration_ms=500, min_speech_duration_ms=100),
                initial_prompt=settings.voice_stt_prompt or None,
                condition_on_previous_text=False,
            )
        except RuntimeError as e:
            err_msg = str(e).lower()
            if "cublas" in err_msg or "cuda" in err_msg:
                logger.warning("CUDA execution failed at transcribe runtime (%s). Rebuilding model on CPU...", e)
                global _stt_model
                _stt_model = None
                settings.voice_stt_device = "cpu"
                model = _get_stt_model()
                segments, info = model.transcribe(
                    tmp_path,
                    language=target_lang,
                    beam_size=beam_size,
                    best_of=beam_size,
                    repetition_penalty=1.2,
                    no_repeat_ngram_size=3,
                    max_new_tokens=96,
                    vad_filter=True,
                    vad_parameters=dict(min_silence_duration_ms=500, min_speech_duration_ms=100),
                    initial_prompt=settings.voice_stt_prompt or None,
                    condition_on_previous_text=False,
                )
            else:
                raise
        # Restrict detected language to allowed set (default: English and Chinese only)
        if not target_lang and allowed_langs and info.language not in allowed_langs:
            probs = dict(info.all_language_probs or [])
            best_allowed = max(allowed_langs, key=lambda l: probs.get(l, 0.0))
            best_prob = probs.get(best_allowed, 0.0)
            logger.warning(
                "Whisper detected '%s' (not in allowed %s, prob=%.2f) — restricting to '%s' (prob=%.3f)",
                info.language, allowed_langs, info.language_probability, best_allowed, best_prob
            )
            if best_prob < 0.03:
                logger.info("Language confidence for %s too low (best=%.3f) — discarding noise",
                            allowed_langs, best_prob)
                return {
                    "text": "",
                    "avg_logprob": 0.0,
                    "no_speech_prob": 1.0,
                    "detected_language": str(info.language or ""),
                    "language_prob": float(info.language_probability or 0.0),
                    "duration_s": float(info.duration or 0.0),
                    "rms_processed": rms_processed,
                }

            target_lang = best_allowed
            segments, info = model.transcribe(
                tmp_path,
                language=target_lang,
                beam_size=beam_size,
                best_of=beam_size,
                repetition_penalty=1.2,
                no_repeat_ngram_size=3,
                max_new_tokens=96,
                vad_filter=True,
                vad_parameters=dict(min_silence_duration_ms=500, min_speech_duration_ms=100),
                initial_prompt=settings.voice_stt_prompt or None,
                condition_on_previous_text=False,
            )
        segs = list(segments)
        text = " ".join(seg.text.strip() for seg in segs).strip()

        # If vad_filter pruned out the short utterance, retry with vad_filter=False
        if not text:
            logger.info("vad_filter=True returned empty segments; retrying with vad_filter=False...")
            segments, info = model.transcribe(
                tmp_path,
                language=target_lang or (info.language if (not allowed_langs or info.language in allowed_langs) else "en"),
                beam_size=beam_size,
                best_of=beam_size,
                repetition_penalty=1.2,
                no_repeat_ngram_size=3,
                max_new_tokens=96,
                vad_filter=False,
                initial_prompt=settings.voice_stt_prompt or None,
                condition_on_previous_text=False,
            )
            segs = list(segments)
            text = " ".join(seg.text.strip() for seg in segs).strip()

        # Strip any foreign scripts outside English (ASCII) and Chinese (\u4e00-\u9fff)
        if allowed_langs and set(allowed_langs) <= {"en", "zh"}:
            clean_text = _re.sub(r"[^\x00-\x7F\u4e00-\u9fff\u3000-\u303f]+", " ", text).strip()
            clean_text = _re.sub(r"\s+", " ", clean_text).strip()
            if clean_text != text:
                logger.info("Filtered non-en/zh characters: '%s' -> '%s'", text, clean_text)
            text = clean_text

        if segs:
            avg_logprob = float(segs[0].avg_logprob)
            no_speech_prob = float(segs[0].no_speech_prob)
        else:
            avg_logprob, no_speech_prob = 0.0, 1.0
        logger.info("STT ok (lang=%s, %.1f s audio, logp=%.2f nsp=%.2f): %.80s",
                    info.language, info.duration, avg_logprob, no_speech_prob, text)
        return {
            "text": text,
            "avg_logprob": avg_logprob,
            "no_speech_prob": no_speech_prob,
            "detected_language": str(info.language or ""),
            "language_prob": float(info.language_probability or 0.0),
            "duration_s": float(info.duration or 0.0),
            "rms_processed": rms_processed,
            "wav_bytes": wav_bytes,
            "samples_out": int(len(pcm16)),
        }
    finally:
        Path(tmp_path).unlink(missing_ok=True)


def _transcribe_pcm(pcm16: np.ndarray, language: str | None) -> tuple[str, float, float]:
    """Write a temp WAV, run faster-whisper.

    Returns (text, first_segment_avg_logprob, first_segment_no_speech_prob).
    The two confidence numbers let the caller reject hallucinated wake words:
    Whisper readily invents short phrases on noise/echo, and those segments
    carry tell-tale stats (high no_speech_prob / low avg_logprob).

    IMPORTANT: never put the wake words themselves in initial_prompt. The
    prompt biases the decoder and was verified to turn room noise / TTS echo
    into exactly the wake word (field: endless self-wake loop). Keep the
    prompt neutral, or empty (settings.voice_stt_prompt).
    """
    res = _transcribe_pcm_impl(pcm16, language, apply_denoise=True, save_capture=True)
    return res["text"], res["avg_logprob"], res["no_speech_prob"]


def check_wake_word(text: str, wake_words: list[str]) -> tuple[bool, str, str]:
    """
    Check if transcript begins with any of the configured wake words.
    Strips repeated leading wake words ("米米 米米" -> wake-only, no command).
    Returns (matched: bool, matched_wake_word: str, command: str).
    """
    cleaned = text.strip()
    if not cleaned:
        return False, "", ""

    # Normalize Japanese Whisper hallucination for "mimi" (耳 = ear in ja)
    if cleaned in ("耳", "耳。", "耳！", "耳？", "にみ", "ミミ"):
        return True, "mimi", ""

    # Normalize internal punctuation (commas, hyphens, colons) to spaces
    # so "Hey, Mimi." -> "Hey Mimi."
    norm = _re.sub(r"^[^\w\s\u4e00-\u9fff]+", "", cleaned)
    norm = _re.sub(r"[,，:：\-—_]+", " ", norm)
    norm = _re.sub(r"\s+", " ", norm).strip()

    # Sort wake words longest first so "hey mimi" matches before "mimi"
    sorted_words = sorted([w.strip().lower() for w in wake_words if w.strip()], key=len, reverse=True)

    matched_word = ""
    while True:
        hit = None
        for w in sorted_words:
            is_ascii = w.isascii()
            boundary = r"\b" if is_ascii else ""
            pat = r"^(?:(?:hey|hi|ok|hello)\s+)?" + _re.escape(w) + boundary + r"[\s!！?？.]*"
            m = _re.match(pat, norm, flags=_re.IGNORECASE)
            if m:
                hit = (w, m.end())
                break
        if not hit:
            break
        if not matched_word:
            matched_word = hit[0]
        norm = norm[hit[1]:].strip()
        # Guard: never loop forever on a zero-length match
        if hit[1] == 0:
            break

    if not matched_word:
        return False, "", ""

    cmd = _re.sub(r"^[,\s，!！?？:：\-—.]+", "", norm).strip()
    return True, matched_word, cmd


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

        # ponytail: subprocess.run in thread pool works on all loops (Proactor, Selector on Windows, uvloop)
        def _run_ffmpeg() -> subprocess.CompletedProcess[bytes]:
            return subprocess.run(
                [
                    "ffmpeg", "-y", "-loglevel", "error", "-i", mp3_path,
                    "-ar", str(ESP_SAMPLE_RATE), "-ac", "1", "-sample_fmt", "s16",
                    wav_path,
                ],
                capture_output=True,
                timeout=30,
            )

        try:
            proc = await asyncio.to_thread(_run_ffmpeg)
        except subprocess.TimeoutExpired:
            raise HTTPException(504, "ffmpeg timed out")
        if proc.returncode != 0:
            err_msg = proc.stderr.decode("utf-8", errors="replace").strip()
            logger.error("ffmpeg failed (code %d): %s", proc.returncode, err_msg)
            raise HTTPException(500, f"ffmpeg failed to convert TTS audio: {err_msg}")
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
    t0 = time.perf_counter()
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

        samples_in = int(len(pcm))
        # Check and restore natural voice pitch if audio was decimated by DMA slot mask (8k -> 16k)
        pcm, pitch_upsampled = check_and_fix_pitch(pcm, ESP_SAMPLE_RATE)
        raw_stats = _audio_stats(pcm)
        rms = raw_stats["rms"]
        logger.info("STT request: %d bytes (%.2fs @ %dHz), RMS=%.1f (threshold=%.1f)",
                    len(pcm) * 2, len(pcm) / ESP_SAMPLE_RATE, rate, rms, settings.voice_min_rms)
        if rms < settings.voice_min_rms:
            # Pure silence / faint electrical noise: skip Whisper entirely (do NOT clobber last valid capture).
            logger.info("STT skipped: rms=%.0f < voice_min_rms=%.0f", rms, settings.voice_min_rms)
            return {"text": "", "raw_text": "", "wake_matched": False, "wake_word": ""}
        stt_res = await asyncio.to_thread(
            _transcribe_pcm_impl,
            pcm,
            settings.voice_language,
            apply_denoise=True,
            save_capture=False,
        )
    except HTTPException:
        raise
    except Exception as e:
        logger.exception("STT failed")
        raise HTTPException(500, f"transcription failed: {e}")

    text = stt_res["text"]
    avg_logprob = stt_res["avg_logprob"]
    no_speech_prob = stt_res["no_speech_prob"]

    wake_list = [w.strip() for w in settings.voice_wake_words.split(",") if w.strip()]
    wake_matched, wake_word, cmd = check_wake_word(text, wake_list)

    # Confidence gate: reject hallucinated wake words. Whisper invents short
    # phrases on noise/TTS-echo; measured on this pipeline, genuine wake
    # utterances have avg_logprob > -1.0 while hallucinations sit near/below
    # it with high no_speech_prob. Keep the device from self-waking forever.
    confidence_rejected = False
    if wake_matched and (
        avg_logprob < settings.voice_min_avg_logprob
        or no_speech_prob > settings.voice_max_no_speech_prob
    ):
        logger.warning(
            "wake word REJECTED by confidence gate: word='%s' logp=%.2f nsp=%.2f raw='%.60s'",
            wake_word, avg_logprob, no_speech_prob, text)
        confidence_rejected = True
        wake_matched, wake_word, cmd = False, "", ""

    logger.info("STT: raw='%s' | wake_matched=%s (word='%s') | cmd='%s'",
                text, wake_matched, wake_word, cmd)

    wav_bytes = stt_res.get("wav_bytes")
    if wav_bytes:
        _store_debug_capture(
            wav_bytes,
            {
                "text": cmd if wake_matched else "",
                "raw_text": text,
                "wake_matched": wake_matched,
                "wake_word": wake_word,
                "avg_logprob": round(avg_logprob, 3),
                "no_speech_prob": round(no_speech_prob, 3),
                "confidence_rejected": confidence_rejected,
                "detected_language": stt_res.get("detected_language", ""),
                "language_prob": round(stt_res.get("language_prob", 0.0), 3),
                "rms_raw": raw_stats["rms"],
                "rms_processed": round(stt_res.get("rms_processed", 0.0), 2),
                "peak": raw_stats["peak"],
                "dc_offset": raw_stats["dc_offset"],
                "samples_in": samples_in,
                "samples": int(stt_res.get("samples_out", len(pcm))),
                "duration_s": round(len(pcm) / ESP_SAMPLE_RATE, 3),
                "pitch_upsampled": pitch_upsampled,
                "latency_ms": round((time.perf_counter() - t0) * 1000, 1),
            },
            write_last_capture=bool(text.strip()),
        )

    return {
        "text": cmd if wake_matched else "",
        "raw_text": text,
        "wake_matched": wake_matched,
        "wake_word": wake_word,
    }


async def _run_stt(pcm: np.ndarray) -> tuple[str, float, float]:
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
        line1, line2 = extract_oled_keywords(req.text)
    except HTTPException:
        raise
    except Exception as e:
        logger.exception("TTS synthesis failed")
        raise HTTPException(500, f"tts failed: {e}")

    headers = {
        "X-OLED-Line1": line1,
        "X-OLED-Line2": line2,
    }
    logger.info("TTS ok: L1='%s', L2='%s' (for text: '%.40s...')",
                line1, line2, req.text)

    return Response(content=wav, media_type="audio/wav", headers=headers)


@router.get("/last-capture-audio")
async def get_last_capture_audio():
    """Serves the most recent audio WAV captured from the physical microphone."""
    wav_path = Path("data/last_capture.wav")
    if not wav_path.exists():
        raise HTTPException(404, "No audio captured yet")
    return Response(
        content=wav_path.read_bytes(),
        media_type="audio/wav",
        headers={
            "Cache-Control": "no-store, no-cache, must-revalidate, max-age=0",
            "Pragma": "no-cache",
        },
    )


@router.get("/captures/{capture_id}")
async def get_capture_audio_by_id(capture_id: str):
    """Serves the exact WAV blob from a specific transcribe capture in the bounded ring."""
    wav = get_capture_wav(capture_id)
    if wav is None:
        raise HTTPException(404, "Capture not found or evicted")
    return Response(
        content=wav,
        media_type="audio/wav",
        headers={
            "Cache-Control": "no-store, no-cache, must-revalidate, max-age=0",
            "Pragma": "no-cache",
        },
    )


@router.get("/debug/last-result", dependencies=[Depends(verify_device_key)])
async def debug_last_result():
    """Return full diagnostic metadata + audio_url for the most recent /transcribe call."""
    meta = get_latest_capture_meta()
    if not meta:
        return {"exists": False}
    return {"exists": True, **meta}


# ---------------------------------------------------------------------------
# Debug & Regression Endpoints (for /stt hardware & voice bench only)
# ---------------------------------------------------------------------------
DEBUG_VOICE_PRESETS = [
    {"id": "zh-CN-XiaoxiaoNeural", "label": "Chinese (Mandarin) — Xiaoxiao (Female)"},
    {"id": "zh-CN-YunxiNeural", "label": "Chinese (Mandarin) — Yunxi (Male)"},
    {"id": "zh-HK-HiuMaanNeural", "label": "Chinese (Cantonese) — HiuMaan (Female)"},
    {"id": "en-US-AriaNeural", "label": "English (US) — Aria (Female)"},
    {"id": "en-US-GuyNeural", "label": "English (US) — Guy (Male)"},
    {"id": "en-GB-SoniaNeural", "label": "English (UK) — Sonia (Female)"},
]


def _audio_stats(pcm: np.ndarray, sample_rate: int = ESP_SAMPLE_RATE) -> dict:
    if len(pcm) == 0:
        return {"samples": 0, "duration_s": 0.0, "rms": 0.0, "peak": 0, "dc_offset": 0.0}
    f = pcm.astype(np.float64)
    return {
        "samples": int(len(pcm)),
        "duration_s": round(len(pcm) / sample_rate, 3),
        "rms": round(float(np.sqrt(np.mean(f ** 2))), 2),
        "peak": int(np.max(np.abs(pcm))),
        "dc_offset": round(float(np.mean(f)), 2),
    }


def _inspect_wav_header(wav: bytes) -> dict:
    if len(wav) < 44 or wav[:4] != b"RIFF" or wav[8:12] != b"WAVE":
        raise ValueError("Invalid RIFF/WAVE header")
    channels = struct.unpack("<H", wav[22:24])[0]
    sample_rate = struct.unpack("<I", wav[24:28])[0]
    bits_per_sample = struct.unpack("<H", wav[34:36])[0]
    pcm = wav_bytes_to_pcm16(wav)
    duration_ms = round((len(pcm) / sample_rate) * 1000, 1) if sample_rate > 0 else 0.0
    return {
        "bytes": len(wav),
        "channels": channels,
        "sample_rate": sample_rate,
        "bits_per_sample": bits_per_sample,
        "samples": len(pcm),
        "duration_ms": duration_ms,
    }


@router.get("/debug/config", dependencies=[Depends(verify_device_key)])
async def debug_voice_config():
    """Return active STT/TTS runtime configuration and last_capture.wav stats."""
    wake_words = [w.strip() for w in settings.voice_wake_words.split(",") if w.strip()]
    last_cap: dict = {"exists": False}
    wav_path = Path("data/last_capture.wav")
    if wav_path.exists():
        try:
            raw = wav_path.read_bytes()
            pcm = wav_bytes_to_pcm16(raw)
            st = _audio_stats(pcm)
            last_cap = {
                "exists": True,
                "size_bytes": len(raw),
                "modified_ts": round(wav_path.stat().st_mtime, 2),
                **st,
            }
        except Exception as e:
            last_cap = {"exists": True, "error": str(e)}

    active_device = "not_loaded"
    if _stt_model is not None:
        active_device = getattr(getattr(_stt_model, "model", None), "device", None) or settings.voice_stt_device

    return {
        "stt_model": settings.voice_stt_model,
        "stt_device_pref": settings.voice_stt_device,
        "stt_compute_type": settings.voice_stt_compute_type,
        "stt_beam_size": settings.voice_stt_beam_size,
        "stt_loaded": _stt_model is not None,
        "stt_active_device": str(active_device),
        "voice_language": settings.voice_language or "auto",
        "voice_allowed_languages": settings.voice_allowed_languages,
        "voice_hw_rate_8k": settings.voice_hw_rate_8k,
        "voice_min_rms": settings.voice_min_rms,
        "voice_min_avg_logprob": settings.voice_min_avg_logprob,
        "voice_max_no_speech_prob": settings.voice_max_no_speech_prob,
        "voice_tts_voice": settings.voice_tts_voice,
        "tts_max_chars": TTS_MAX_CHARS,
        "wake_words": wake_words,
        "last_capture": last_cap,
    }


@router.get("/debug/voices", dependencies=[Depends(verify_device_key)])
async def debug_voice_presets():
    """Return preset edge-tts voices and the current default voice."""
    return {
        "default_voice": settings.voice_tts_voice,
        "voices": DEBUG_VOICE_PRESETS,
    }


@router.post("/debug/transcribe", dependencies=[Depends(verify_device_key)])
async def debug_transcribe(
    request: Request,
    language: str | None = Query(None),
    skip_pitch_fix: bool = Query(False),
    skip_denoise: bool = Query(False),
    skip_rms_gate: bool = Query(False),
    skip_confidence_gate: bool = Query(False),
    save_capture: bool = Query(False),
):
    """
    Debug STT endpoint: accepts raw PCM/WAV upload OR JSON {"use_last_capture": true, ...}.
    Returns full acoustic & Whisper confidence diagnostics without altering /api/voice/transcribe.
    """
    t0 = time.perf_counter()
    ctype = (request.headers.get("content-type") or "").lower()
    body = await request.body()

    use_last_capture = False
    if "application/json" in ctype:
        try:
            payload = json.loads(body.decode("utf-8") or "{}")
        except Exception as e:
            raise HTTPException(400, f"Invalid JSON payload: {e}")
        use_last_capture = bool(payload.get("use_last_capture", False))
        if "language" in payload:
            language = payload.get("language")
        skip_pitch_fix = bool(payload.get("skip_pitch_fix", skip_pitch_fix))
        skip_denoise = bool(payload.get("skip_denoise", skip_denoise))
        skip_rms_gate = bool(payload.get("skip_rms_gate", skip_rms_gate))
        skip_confidence_gate = bool(payload.get("skip_confidence_gate", skip_confidence_gate))
        save_capture = bool(payload.get("save_capture", save_capture))

    if use_last_capture:
        wav_path = Path("data/last_capture.wav")
        if not wav_path.exists():
            raise HTTPException(404, "No data/last_capture.wav recorded yet")
        pcm = wav_bytes_to_pcm16(wav_path.read_bytes())
        # last_capture.wav was already saved post-upsample/denoise; default to not double-upsampling
        pitch_upsampled = False
    else:
        if not body:
            raise HTTPException(400, "empty audio body")
        if len(body) > 6_000_000:
            raise HTTPException(413, "audio too large")
        try:
            raw_rate = int(request.headers.get("x-audio-rate") or ESP_SAMPLE_RATE)
        except ValueError:
            raw_rate = ESP_SAMPLE_RATE
        rate = raw_rate if raw_rate > 0 else ESP_SAMPLE_RATE

        if "wav" in ctype or body[:4] == b"RIFF":
            pcm = wav_bytes_to_pcm16(body)
        else:
            raw = np.frombuffer(body[: len(body) // 2 * 2], dtype="<i2").copy()
            pcm = resample_to_16k(raw, rate) if rate != ESP_SAMPLE_RATE else raw

        pitch_upsampled = False
        if not skip_pitch_fix:
            pcm, pitch_upsampled = check_and_fix_pitch(pcm, ESP_SAMPLE_RATE)

    raw_stats = _audio_stats(pcm)
    lang_override = None if (not language or language == "auto") else language.strip().lower()
    if lang_override is None and not language:
        lang_override = settings.voice_language

    if not skip_rms_gate and raw_stats["rms"] < settings.voice_min_rms:
        return {
            "ok": True,
            "text": "",
            "raw_text": "",
            "wake_matched": False,
            "wake_word": "",
            "avg_logprob": 0.0,
            "no_speech_prob": 1.0,
            "confidence_rejected": False,
            "detected_language": "",
            "language_prob": 0.0,
            "rms_raw": raw_stats["rms"],
            "rms_processed": raw_stats["rms"],
            "peak": raw_stats["peak"],
            "dc_offset": raw_stats["dc_offset"],
            "samples": raw_stats["samples"],
            "duration_s": raw_stats["duration_s"],
            "pitch_upsampled": pitch_upsampled,
            "skipped_reason": f"rms_below_threshold ({raw_stats['rms']} < {settings.voice_min_rms})",
            "latency_ms": round((time.perf_counter() - t0) * 1000, 1),
        }

    stt_res = await asyncio.to_thread(
        _transcribe_pcm_impl,
        pcm,
        lang_override,
        apply_denoise=not skip_denoise,
        save_capture=save_capture,
    )
    text = stt_res["text"]
    avg_logprob = stt_res["avg_logprob"]
    no_speech_prob = stt_res["no_speech_prob"]

    wake_list = [w.strip() for w in settings.voice_wake_words.split(",") if w.strip()]
    wake_matched, wake_word, cmd = check_wake_word(text, wake_list)

    confidence_rejected = False
    if wake_matched and not skip_confidence_gate and (
        avg_logprob < settings.voice_min_avg_logprob
        or no_speech_prob > settings.voice_max_no_speech_prob
    ):
        confidence_rejected = True
        wake_matched, wake_word, cmd = False, "", ""

    return {
        "ok": True,
        "text": cmd if wake_matched else "",
        "raw_text": text,
        "wake_matched": wake_matched,
        "wake_word": wake_word,
        "avg_logprob": round(avg_logprob, 3),
        "no_speech_prob": round(no_speech_prob, 3),
        "confidence_rejected": confidence_rejected,
        "detected_language": stt_res["detected_language"],
        "language_prob": round(stt_res["language_prob"], 3),
        "rms_raw": raw_stats["rms"],
        "rms_processed": round(stt_res["rms_processed"], 2),
        "peak": raw_stats["peak"],
        "dc_offset": raw_stats["dc_offset"],
        "samples": raw_stats["samples"],
        "duration_s": raw_stats["duration_s"],
        "pitch_upsampled": pitch_upsampled,
        "skipped_reason": "",
        "latency_ms": round((time.perf_counter() - t0) * 1000, 1),
    }


@router.post("/debug/tts", dependencies=[Depends(verify_device_key)])
async def debug_tts(req: TTSRequest):
    """Synthesize speech with timing + WAV contract headers for the /stt debug bench."""
    t0 = time.perf_counter()
    text = sanitize_tts_text(req.text.strip())
    if not text:
        raise HTTPException(400, "text is empty after sanitization (emoji-only reply?)")
    if len(text) > 2000:
        raise HTTPException(413, "text too long for voice playback (max 2000 chars)")

    voice = (req.voice or "").strip() or settings.voice_tts_voice
    try:
        wav = await _tts_to_wav(text, voice)
        line1, line2 = extract_oled_keywords(req.text)
        wav_info = _inspect_wav_header(wav)
    except HTTPException:
        raise
    except Exception as e:
        logger.exception("Debug TTS synthesis failed")
        raise HTTPException(500, f"tts failed: {e}")

    latency_ms = round((time.perf_counter() - t0) * 1000, 1)
    headers = {
        "X-OLED-Line1": line1,
        "X-OLED-Line2": line2,
        "X-TTS-Latency-Ms": str(latency_ms),
        "X-WAV-Bytes": str(wav_info["bytes"]),
        "X-WAV-Duration-Ms": str(wav_info["duration_ms"]),
        "X-WAV-Sample-Rate": str(wav_info["sample_rate"]),
        "X-WAV-Channels": str(wav_info["channels"]),
        "X-WAV-Bits": str(wav_info["bits_per_sample"]),
        "X-Voice-Used": voice,
        "X-Sanitized-Len": str(len(text)),
        "Access-Control-Expose-Headers": (
            "X-OLED-Line1, X-OLED-Line2, X-TTS-Latency-Ms, X-WAV-Bytes, "
            "X-WAV-Duration-Ms, X-WAV-Sample-Rate, X-WAV-Channels, X-WAV-Bits, "
            "X-Voice-Used, X-Sanitized-Len"
        ),
    }
    return Response(content=wav, media_type="audio/wav", headers=headers)


class RegressionRequest(BaseModel):
    include_tts: bool = True
    include_loopback: bool = True
    voice: str | None = None


@router.post("/debug/regression", dependencies=[Depends(verify_device_key)])
async def debug_regression(req: RegressionRequest | None = None):
    """
    Non-destructive automated regression suite for STT, TTS, DSP, and wake-word logic.
    Runs strictly in-memory / temp files and never overwrites data/last_capture.wav.
    """
    cfg = req or RegressionRequest()
    t_suite0 = time.perf_counter()
    cases: list[dict] = []
    wake_list = [w.strip() for w in settings.voice_wake_words.split(",") if w.strip()]

    def _run_sync_case(name: str, fn):
        t0 = time.perf_counter()
        try:
            detail = fn()
            cases.append({
                "name": name,
                "passed": True,
                "duration_ms": round((time.perf_counter() - t0) * 1000, 1),
                "detail": detail,
            })
        except Exception as exc:
            cases.append({
                "name": name,
                "passed": False,
                "duration_ms": round((time.perf_counter() - t0) * 1000, 1),
                "detail": f"{type(exc).__name__}: {exc}",
            })

    def _case_wake_en():
        matched, word, cmd = check_wake_word("Hey Mimi turn on the white LED", wake_list)
        assert matched and word == "hey mimi" and cmd == "turn on the white LED", (matched, word, cmd)
        return f"matched='{word}', cmd='{cmd}'"

    def _case_wake_zh():
        matched, word, cmd = check_wake_word("米米 你好", wake_list)
        assert matched and word == "米米" and cmd == "你好", (matched, word, cmd)
        return f"matched='{word}', cmd='{cmd}'"

    def _case_wake_negative():
        for phrase in ("Thanks for watching!", "the weather is sunny", ""):
            matched, word, cmd = check_wake_word(phrase, wake_list)
            assert not matched, f"False positive on '{phrase}': {(matched, word, cmd)}"
        return "3 negative phrases rejected"

    def _case_pcm_wav_roundtrip():
        samples = np.array([0, 1200, -1200, 32767, -32768, 42], dtype=np.int16)
        wav = pcm_to_wav_bytes(samples, ESP_SAMPLE_RATE)
        info = _inspect_wav_header(wav)
        out = wav_bytes_to_pcm16(wav)
        assert info["sample_rate"] == 16000 and info["channels"] == 1 and info["bits_per_sample"] == 16, info
        assert np.array_equal(samples, out), "PCM roundtrip mismatch"
        return f"RIFF/WAVE 16kHz 1ch 16-bit verified ({len(wav)} bytes)"

    def _case_resample_8k():
        src = np.zeros(8000, dtype=np.int16)
        dst = resample_to_16k(src, 8000)
        assert len(dst) == 16000, len(dst)
        return "8000 -> 16000 samples"

    def _case_denoise_notch():
        t = np.arange(ESP_SAMPLE_RATE) / float(ESP_SAMPLE_RATE)
        # 169.2 Hz OLED ripple + DC offset 2000
        sig = (2000.0 + 1500.0 * np.sin(2.0 * np.pi * 169.2 * t)).astype(np.int16)
        cleaned = denoise_and_boost_audio(sig, ESP_SAMPLE_RATE)
        dc_after = abs(float(np.mean(cleaned.astype(np.float64))))
        assert dc_after < 50.0, f"DC offset remained high: {dc_after}"
        return f"DC offset reduced 2000 -> {dc_after:.1f}"

    def _case_silence_gate():
        quiet = np.full(16000, 10, dtype=np.int16)
        rms = float(np.sqrt(np.mean(quiet.astype(np.float64) ** 2)))
        assert rms < settings.voice_min_rms, (rms, settings.voice_min_rms)
        return f"RMS {rms:.1f} < gate {settings.voice_min_rms}"

    def _case_stt_synthetic():
        rng = np.random.default_rng(42)
        noise = rng.normal(0, 100, 16000).astype(np.int16)
        res = _transcribe_pcm_impl(noise, "en", apply_denoise=True, save_capture=False)
        matched, word, _ = check_wake_word(res["text"], wake_list)
        if matched and (
            res["avg_logprob"] < settings.voice_min_avg_logprob
            or res["no_speech_prob"] > settings.voice_max_no_speech_prob
        ):
            matched = False
        assert not matched, f"Synthetic noise triggered wake word '{word}' ({res})"
        return f"model ok, text='{res['text']}', logp={res['avg_logprob']:.2f}, nsp={res['no_speech_prob']:.2f}"

    def _case_tts_sanitize_and_oled():
        clean = sanitize_tts_text("LED is on! 🎉🔥✅")
        assert "🎉" not in clean and "LED is on!" in clean, clean
        l1, l2 = extract_oled_keywords("Turned on the white LED")
        assert len(l1) <= 16 and len(l2) <= 16 and l1.isascii() and l2.isascii(), (l1, l2)
        return f"sanitized='{clean}', oled=('{l1}', '{l2}')"

    _run_sync_case("wake_word_positive_en", _case_wake_en)
    _run_sync_case("wake_word_positive_zh", _case_wake_zh)
    _run_sync_case("wake_word_negative_hallucination", _case_wake_negative)
    _run_sync_case("pcm_wav_roundtrip", _case_pcm_wav_roundtrip)
    _run_sync_case("resample_8k_to_16k", _case_resample_8k)
    _run_sync_case("denoise_dc_and_notch", _case_denoise_notch)
    _run_sync_case("stt_silence_gate", _case_silence_gate)
    await asyncio.to_thread(_run_sync_case, "stt_synthetic_no_wake_hallucination", _case_stt_synthetic)
    _run_sync_case("tts_sanitize_and_oled", _case_tts_sanitize_and_oled)

    if cfg.include_tts:
        voice = (cfg.voice or "").strip() or settings.voice_tts_voice
        t_tts0 = time.perf_counter()
        tts_wav: bytes | None = None
        try:
            tts_wav = await _tts_to_wav("Hey Mimi, turn on the white light.", voice)
            info = _inspect_wav_header(tts_wav)
            assert info["sample_rate"] == 16000 and info["channels"] == 1 and info["bits_per_sample"] == 16, info
            assert info["bytes"] > 44 and info["duration_ms"] > 300, info
            cases.append({
                "name": "tts_wav_contract",
                "passed": True,
                "duration_ms": round((time.perf_counter() - t_tts0) * 1000, 1),
                "detail": f"{info['bytes']}B, {info['duration_ms']}ms @ {info['sample_rate']}Hz {info['channels']}ch",
            })
        except Exception as exc:
            cases.append({
                "name": "tts_wav_contract",
                "passed": False,
                "duration_ms": round((time.perf_counter() - t_tts0) * 1000, 1),
                "detail": f"{type(exc).__name__}: {exc}",
            })

        if cfg.include_loopback and tts_wav is not None:
            def _case_loopback():
                pcm = wav_bytes_to_pcm16(tts_wav)
                res = _transcribe_pcm_impl(pcm, "en", apply_denoise=False, save_capture=False)
                assert res["text"].strip(), "Empty loopback transcription"
                matched, word, cmd = check_wake_word(res["text"], wake_list)
                return f"raw='{res['text']}', wake={matched} ('{word}'), cmd='{cmd}'"

            await asyncio.to_thread(_run_sync_case, "tts_to_stt_loopback", _case_loopback)

    passed = sum(1 for c in cases if c["passed"])
    failed = len(cases) - passed
    return {
        "ok": failed == 0,
        "passed": passed,
        "failed": failed,
        "total": len(cases),
        "duration_ms": round((time.perf_counter() - t_suite0) * 1000, 1),
        "cases": cases,
    }
