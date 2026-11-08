"""
Standalone test and latency benchmark for STT acceleration and device fallback.

Run:
    python tests/test_stt_speed.py
or:
    python -m pytest tests/test_stt_speed.py -v
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

import numpy as np

# Ensure backend root is on PYTHONPATH
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from api.voice_router import _get_stt_model, _setup_windows_cuda_dlls, _transcribe_pcm
from config import settings


def test_windows_cuda_dll_helper_runs():
    """Verify DLL search helper runs safely on any platform."""
    _setup_windows_cuda_dlls()


def test_stt_model_initialization():
    """Verify STT model loads with configured default ('base')."""
    model = _get_stt_model()
    assert model is not None
    assert settings.voice_stt_model == "base"
    assert settings.voice_stt_beam_size == 1


def test_stt_transcribe_pcm_synthetic_latency():
    """Verify transcribe_pcm runs with greedy decoding and low latency."""
    # 1 second of 16 kHz mono 16-bit PCM (silence/low white noise)
    rng = np.random.default_rng(42)
    noise = (rng.normal(0, 100, 16000)).astype(np.int16)

    t0 = time.perf_counter()
    text, avg_logprob, no_speech_prob = _transcribe_pcm(noise, language="en")
    elapsed = time.perf_counter() - t0

    print(f"\n[STT Benchmark] 1.0s audio transcribe latency: {elapsed * 1000:.1f} ms")
    print(f"[STT Benchmark] text='{text}', logprob={avg_logprob:.2f}, no_speech={no_speech_prob:.2f}")

    assert isinstance(text, str)
    assert isinstance(avg_logprob, float)
    assert isinstance(no_speech_prob, float)
    # CPU latency for 1s on 'base' model with greedy decoding is typically 300-800ms
    # Allow up to 3.0s as generous test ceiling for CI/loaded CPU machines
    assert elapsed < 3.0, f"STT took too long: {elapsed:.2f}s"


if __name__ == "__main__":
    print("Running STT speed & device verification tests...")
    test_windows_cuda_dll_helper_runs()
    print("  ✓ DLL directory helper passed")
    test_stt_model_initialization()
    print("  ✓ Model initialization passed")
    test_stt_transcribe_pcm_synthetic_latency()
    print("  ✓ Transcribe latency test passed")
    print("ALL STT TESTS PASSED.")
