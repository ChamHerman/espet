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
import logging
import math
import os
import struct
import sys
import tempfile
from pathlib import Path
import subprocess
import numpy as np
from fastapi import APIRouter, Depends, HTTPException, Request
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
    Restore natural human voice pitch and normal 1x playback speed.
    Because the ESP32 mic DMA slot mask captures at 8,000 samples/sec into a 16 kHz buffer,
    audio arrives at 2x playback speed (Minion/chipmunk voice).
    Upsamples 8k -> 16k so duration, tempo, and pitch are 100% natural.
    """
    if getattr(settings, "voice_hw_rate_8k", True):
        logger.info(
            "Restoring normal 1x tempo and pitch (resampling 8k -> 16k, %d -> %d samples)",
            len(pcm16), len(pcm16) * 2
        )
        corrected = resample_to_16k(pcm16, 8000)
        return corrected, True

    # Fallback to pitch-based gate if voice_hw_rate_8k is disabled
    f0 = detect_pitch_f0(pcm16, sample_rate)
    if f0 > 170.0:
        logger.info(
            "Speech pitch is %.1f Hz (> 170 Hz) — restoring normal pitch (resampling 8k -> 16k)",
            f0
        )
        corrected = resample_to_16k(pcm16, 8000)
        new_f0 = detect_pitch_f0(corrected, sample_rate)
        logger.info("Normal voice pitch restored: %.1f Hz", new_f0)
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
    if len(pcm16) < ESP_SAMPLE_RATE // 5:  # < 0.2 s of audio
        return "", 0.0, 1.0

    # Clean electrical buzz (OLED charge pump 169 Hz harmonics) and normalize human voice
    pcm16 = denoise_and_boost_audio(pcm16)

    model = _get_stt_model()
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        wav_bytes = pcm_to_wav_bytes(pcm16)
        f.write(wav_bytes)
        tmp_path = f.name
    # Save last audio capture for inspection
    try:
        debug_file = Path("data/last_capture.wav")
        debug_file.parent.mkdir(parents=True, exist_ok=True)
        debug_file.write_bytes(wav_bytes)
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
                return "", 0.0, 1.0

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
        return text, avg_logprob, no_speech_prob
    finally:
        Path(tmp_path).unlink(missing_ok=True)


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

        # Check and restore natural voice pitch if audio was decimated by DMA slot mask (8k -> 16k)
        pcm, _ = check_and_fix_pitch(pcm, ESP_SAMPLE_RATE)

        rms = float(np.sqrt(np.mean(pcm.astype(np.float64) ** 2))) if len(pcm) else 0.0
        logger.info("STT request: %d bytes (%.2fs @ %dHz), RMS=%.1f (threshold=%.1f)",
                    len(pcm) * 2, len(pcm) / ESP_SAMPLE_RATE, rate, rms, settings.voice_min_rms)
        if rms < settings.voice_min_rms:
            # Pure silence / faint electrical noise: skip Whisper entirely.
            logger.info("STT skipped: rms=%.0f < voice_min_rms=%.0f", rms, settings.voice_min_rms)
            return {"text": "", "raw_text": "", "wake_matched": False, "wake_word": ""}
        text, avg_logprob, no_speech_prob = await _run_stt(pcm)
    except HTTPException:
        raise
    except Exception as e:
        logger.exception("STT failed")
        raise HTTPException(500, f"transcription failed: {e}")

    wake_list = [w.strip() for w in settings.voice_wake_words.split(",") if w.strip()]
    wake_matched, wake_word, cmd = check_wake_word(text, wake_list)

    # Confidence gate: reject hallucinated wake words. Whisper invents short
    # phrases on noise/TTS-echo; measured on this pipeline, genuine wake
    # utterances have avg_logprob > -1.0 while hallucinations sit near/below
    # it with high no_speech_prob. Keep the device from self-waking forever.
    if wake_matched and (
        avg_logprob < settings.voice_min_avg_logprob
        or no_speech_prob > settings.voice_max_no_speech_prob
    ):
        logger.warning(
            "wake word REJECTED by confidence gate: word='%s' logp=%.2f nsp=%.2f raw='%.60s'",
            wake_word, avg_logprob, no_speech_prob, text)
        wake_matched, wake_word, cmd = False, "", ""

    logger.info("STT: raw='%s' | wake_matched=%s (word='%s') | cmd='%s'",
                text, wake_matched, wake_word, cmd)

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
        wav, (line1, line2) = await asyncio.gather(
            _tts_to_wav(text, voice),
            extract_oled_keywords_llm(req.text),
        )
    except HTTPException:
        raise
    except Exception as e:
        logger.exception("TTS/OLED distillation failed")
        raise HTTPException(500, f"tts failed: {e}")

    headers = {
        "X-OLED-Line1": line1,
        "X-OLED-Line2": line2,
    }
    logger.info("TTS OLED keywords (LLM): L1='%s', L2='%s' (for text: '%.40s...')",
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
        headers={"Cache-Control": "no-cache, no-store, must-revalidate"},
    )
