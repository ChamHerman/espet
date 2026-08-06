#pragma once

/*
 * tool_audio.h — ESP32-S3 I2S Microphone Capture (INMP441)
 *
 * Current mode: RX-only (no speaker connected).
 *
 * Hardware (I2S_NUM_0, ESP32 as master):
 *   GPIO 41  BCLK  — ESP32 drives clock  → INMP441 SCK
 *   GPIO 42  WS    — ESP32 drives WS     → INMP441 WS
 *   GPIO  1  DIN   — INMP441 SD (data)   → ESP32 RX
 *   GPIO  2  DOUT  — unused (MAX98357A DIN — no speaker yet)
 *
 * Signal flow (input):
 *   Sound → INMP441 (analog→digital I2S) → GPIO 1 → ESP32 DMA RX buffer
 *
 * Current scope (hardware verification):
 *   - Hold green button → capture voice as PCM into PSRAM
 *   - Release           → dump stats + hex dump to serial monitor
 *   - No HTTP, no Whisper, no LLM invocation yet
 *
 * TODO (voice pipeline):
 *   - [PIPELINE-1] POST PCM to /api/transcribe → transcript text
 *   - [PIPELINE-2] Push transcript into agent_loop message bus
 *   - [PIPELINE-3] Receive TTS WAV from /api/tts
 *   - [PIPELINE-4] Add TX channel (MAX98357A) for speaker playback
 */

#include "esp_err.h"
#include "tools/tool_registry.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */

/**
 * @brief Initialize the I2S peripheral in full-duplex mode.
 *
 * Allocates both TX (MAX98357A playback) and RX (INMP441 capture) channels
 * on I2S_NUM_0 sharing BCLK + WS. Must be called once from app_main before
 * any PTT or playback operations.
 *
 * @return ESP_OK on success, ESP_FAIL / driver error codes on failure.
 */
esp_err_t tool_audio_init(void);

/* -----------------------------------------------------------------------
 * Push-To-Talk (PTT) capture — called by button_handler.c callbacks
 * --------------------------------------------------------------------- */

/**
 * @brief Start capturing audio from the INMP441 microphone.
 *
 * Called when the green button (GPIO 39) is pressed. Enables the I2S RX
 * channel and spawns a FreeRTOS capture task that DMA-reads PCM into a
 * PSRAM buffer until tool_audio_ptt_record_stop() is called.
 *
 * Safe to call multiple times — ignored if already recording.
 */
void tool_audio_ptt_record_start(void);

/**
 * @brief Stop audio capture and dump diagnostics to serial.
 *
 * Called when the green button (GPIO 39) is released. Signals the capture
 * task to stop, then prints to serial:
 *   - Recording duration (ms)
 *   - Sample count
 *   - Min / max amplitude (signed 16-bit)
 *   - RMS energy
 *   - First 64 samples as hex + decimal
 *   - Qualitative verdict (silence / low signal / audio detected)
 *
 * TODO [PIPELINE-1]: After the dump, send PCM to backend for transcription.
 *
 * Safe to call when not recording — no-op.
 */
void tool_audio_ptt_record_stop(void);

/* -----------------------------------------------------------------------
 * Accessors for captured audio (used by future pipeline stages)
 * --------------------------------------------------------------------- */

/**
 * @brief Get a pointer to the last captured PCM buffer.
 *
 * Valid only after tool_audio_ptt_record_stop() returns and before the
 * next tool_audio_ptt_record_start() call. The buffer is allocated in PSRAM.
 *
 * TODO [PIPELINE-1]: Pass this to the HTTP upload function.
 *
 * @param[out] out_samples  Number of 16-bit samples in the buffer.
 * @param[out] out_duration_ms  Actual recording duration in milliseconds.
 * @return Pointer to int16_t PCM buffer, or NULL if no capture has been done.
 */
const int16_t *tool_audio_get_last_capture(size_t *out_samples, uint32_t *out_duration_ms);

/* -----------------------------------------------------------------------
 * mimi_tool_t descriptors — registered in tool_registry.c
 * --------------------------------------------------------------------- */

/**
 * @brief LLM-callable tool: play a 440 Hz test tone through the speaker.
 *
 * Allows the LLM agent or a serial CLI invocation to verify the speaker
 * and amplifier are working correctly without needing the green button.
 *
 * Input schema: {} (no parameters)
 * Output: "Tone played: 440Hz, 500ms" or error string.
 *
 * TODO [PIPELINE-4]: Replace with actual TTS playback executor once
 *                    the pipeline is wired up.
 */
extern mimi_tool_t audio_play_tone_tool;

/**
 * @brief LLM-callable tool: record for a fixed 3 seconds and dump stats.
 *
 * Allows the LLM to trigger a timed mic capture for diagnostic purposes
 * without requiring a physical button press.
 *
 * Input schema: { "duration_ms": integer (optional, default 3000, max 10000) }
 * Output: JSON with sample_count, duration_ms, rms, min_amplitude, max_amplitude.
 *
 * TODO [PIPELINE-1]: Replace dump with HTTP POST to backend transcription
 *                    endpoint once pipeline is ready.
 */
extern mimi_tool_t audio_record_test_tool;

#ifdef __cplusplus
}
#endif
