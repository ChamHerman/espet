#pragma once

/*
 * tool_audio.h — ESP32-S3 I2S Audio: INMP441 Microphone (RX) + MAX98357A Speaker (TX)
 *
 * Full-duplex TX+RX pair on I2S_NUM_0, both channels enabled from init.
 *
 * Hardware (ESP32 as master, per .agent/ESPET_PINS.MD):
 *   GPIO 38  BCLK  — shared: ESP32 drives clock → INMP441 SCK + MAX98357A BCLK
 *   GPIO 39  WS    — shared: ESP32 drives WS    → INMP441 WS  + MAX98357A LRC
 *   GPIO 40  DIN   — INMP441 SD (data)   → ESP32 RX
 *   GPIO 21  DOUT  — ESP32 TX data       → MAX98357A DIN (speaker)
 *
 * Signal flow:
 *   Input:  sound → INMP441 → I2S → GPIO 40 → ESP32 DMA RX → PSRAM PCM
 *   Output: sine/PCM → ESP32 DMA TX → GPIO 21 → MAX98357A → speaker
 *
 * Scope:
 *   - Green button (GPIO 41) PTT → capture voice as PCM into PSRAM,
 *     release → dump stats + hex dump to serial monitor
 *   - audio_play_tone  : sine playback through the speaker
 *
 * TODO (voice pipeline):
 *   - [PIPELINE-1] POST PCM to /api/transcribe → transcript text
 *   - [PIPELINE-2] Push transcript into agent_loop message bus
 *   - [PIPELINE-3] Receive TTS WAV from /api/tts
 *   - [PIPELINE-4] Play TTS via the TX channel (playback path now exists)
 */

#include "esp_err.h"
#include "tools/tool_registry.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* I2S capture/playback sample rate — shared with the backend voice contract
 * (16 kHz mono 16-bit; see voice/voice_client.c and api/voice_router.py). */
#define TOOL_AUDIO_SAMPLE_RATE 16000

/* -----------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */

/**
 * @brief Initialize I2S full-duplex on I2S_NUM_0.
 *
 * Allocates the TX (MAX98357A playback) + RX (INMP441 capture) channel pair
 * sharing BCLK + WS, and enables both. Must be called once from
 * tool_registry_init before any PTT, tone, or record operations.
 *
 * @return ESP_OK on success, ESP_FAIL / driver error codes on failure.
 */
esp_err_t tool_audio_init(void);

/* -----------------------------------------------------------------------
 * Wake-word voice flow capture — used by voice/voice_client.c
 *
 * Replaces the old push-to-talk API: the mic stream is now consumed
 * continuously by the WakeNet listener, and command captures are driven
 * by the caller's task (arm → rx_read loop → disarm).
 * --------------------------------------------------------------------- */

/**
 * @brief True while a capture or playback is in progress (bus busy).
 */
bool tool_audio_is_busy(void);

/**
 * @brief Arm a capture into the internal PSRAM buffer (no worker task).
 *
 * The caller's task pumps tool_audio_rx_read() until it returns false or
 * out_full is set, then calls tool_audio_capture_disarm(). Queued-but-
 * unread audio is flushed first so the capture starts "now".
 *
 * @param max_samples Capture limit in samples (clamped to the buffer size).
 * @return false if the bus is busy or audio is not initialized.
 */
bool tool_audio_capture_arm(size_t max_samples);

/**
 * @brief Arm capture with pre-roll samples preserved from the RX ring.
 *
 * @param max_samples     Capture limit in samples.
 * @param preroll_samples Samples from before the trigger moment to preserve
 *                        (e.g. 4000 = 250 ms so the first syllable isn't cut off).
 */
bool tool_audio_capture_arm_preroll(size_t max_samples, size_t preroll_samples);

/**
 * @brief Read one RX chunk while a capture is armed.
 *
 * Copies the converted mono samples into out (up to max_out) AND
 * accumulates them into the internal PSRAM capture buffer.
 *
 * @param out          Scratch buffer for the chunk (may be NULL to skip).
 * @param max_out      Capacity of out in samples.
 * @param out_written  Samples written to out this call.
 * @param captured_total  Total samples accumulated so far (may be NULL).
 * @param out_full     Set true when the internal buffer hit max_samples.
 * @param timeout_ms   Read timeout for this chunk.
 * @return true while the capture is still armed.
 */
bool tool_audio_rx_read(int16_t *out, size_t max_out, size_t *out_written,
                        size_t *captured_total, bool *out_full, uint32_t timeout_ms);

/**
 * @brief Stop the armed capture and stamp its duration.
 *
 * Results are available via tool_audio_get_last_capture().
 */
void tool_audio_capture_disarm(void);

/* -----------------------------------------------------------------------
 * RX stream (ring buffer) — feeds the WakeNet listener gap-free audio
 * --------------------------------------------------------------------- */

/**
 * @brief Start the high-priority mic feeder task.
 *
 * Drains the I2S DMA (only ~128 ms capacity) into a 4 s PSRAM ring so
 * low-priority consumers never lose audio. Idempotent.
 */
esp_err_t tool_audio_rx_stream_start(void);

/**
 * @brief Stop the mic feeder task (asynchronous; task self-deletes).
 */
void tool_audio_rx_stream_stop(void);

/**
 * @brief Read up to max_out samples from the RX ring (blocking up to
 *        timeout_ms for the first sample). Returns samples written.
 */
size_t tool_audio_ring_read(int16_t *out, size_t max_out, uint32_t timeout_ms);

/**
 * @brief Samples currently buffered in the RX ring.
 */
size_t tool_audio_ring_available(void);

/**
 * @brief Drop all buffered audio in the RX ring (resumes from "now").
 */
void tool_audio_ring_flush(void);

/**
 * @brief Clear all audio caches (drains both I2S hardware DMA FIFO and software ring buffer).
 */
void tool_audio_clear_cache(void);

/**
 * @brief Begin/end a speaker session: the RX feeder parks for the whole
 *        window (TTS download + playback), keeping the I2S TX path clean.
 *        Session audio otherwise corrupted (field-verified).
 */
void tool_audio_speaker_session_begin(void);
void tool_audio_speaker_session_end(void);

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

/**
 * @brief Start manual recording into the internal PCM buffer on demand.
 */
esp_err_t tool_audio_manual_record_start(void);

/**
 * @brief Stop manual recording and compute stats.
 */
esp_err_t tool_audio_manual_record_stop(size_t *out_samples, uint32_t *out_dur_ms, double *out_rms);

/* -----------------------------------------------------------------------
 * WAV playback — voice pipeline output [PIPELINE-4]
 * --------------------------------------------------------------------- */

/**
 * @brief Play a full WAV blob through the MAX98357A speaker (blocking).
 *
 * Expects the format served by the backend POST /api/voice/tts endpoint:
 * PCM WAV, 16 kHz, mono, 16-bit. A proper RIFF chunk walk locates the
 * data chunk, so extra leading chunks are tolerated. Stereo input plays
 * its left slot; other sample rates play pitch-shifted with a warning.
 *
 * Fails politely with ESP_ERR_INVALID_STATE if a capture or another
 * playback is already running.
 *
 * @param wav  Pointer to the WAV blob (PSRAM recommended for large files).
 * @param len  Blob length in bytes.
 * @return ESP_OK on success, driver/argument errors otherwise.
 */
esp_err_t tool_audio_play_wav(const uint8_t *wav, size_t len);

/**
 * @brief Short audible feedback cue (record start/stop, pipeline states).
 *
 * Thin wrapper over the internal tone generator. Safe no-op if the audio
 * bus is busy with a capture or longer playback.
 */
void tool_audio_beep(uint32_t freq_hz, uint32_t duration_ms, float vol);

/* -----------------------------------------------------------------------
 * mimi_tool_t descriptors — registered in tool_registry.c
 * --------------------------------------------------------------------- */

/**
 * @brief LLM-callable tool: play a sine tone through the MAX98357A speaker.
 *
 * Allows the LLM agent or a serial CLI invocation to verify the speaker
 * and amplifier are working correctly without needing the green button.
 *
 * Input schema:
 *   { "freq_hz": integer (default 440, 20-8000),
 *     "duration_ms": integer (default 500, max 5000),
 *     "volume": number (0.05-1.0, default 0.5) }
 * Output: "Tone played: 440Hz, 500ms" or error string.
 */
extern mimi_tool_t audio_play_tone_tool;

/**
 * @brief LLM-callable tool: record for a fixed duration and dump stats.
 *
 * Allows the LLM to trigger a timed mic capture for diagnostic purposes
 * without requiring a physical button press.
 *
 * Input schema: { "duration_ms": integer (optional, default 3000, max 10000) }
 * Output: JSON with sample_count, duration_ms, rms, min, max, verdict.
 *
 * TODO [PIPELINE-1]: Replace dump with HTTP POST to backend transcription
 *                    endpoint once pipeline is ready.
 */
extern mimi_tool_t audio_record_test_tool;

/**
 * @brief LLM-callable tool: 4-stage speaker diagnostic (no server needed
 *        for stages 1-3; stage 4 exercises the full server TTS pipeline).
 *
 *   1. Hardware tone    — 440 Hz full-volume sine (1 s): amp/wiring/power.
 *   2. Local WAV        — generated 1 kHz WAV played through the RIFF
 *                         parser + play_wav path (no network).
 *   3. Volume ramp      — 1 kHz at 1.0 / 0.4 / 0.15: audibility threshold
 *                         (TTS speech sits at ~6% full scale!).
 *   4. Full pipeline    — server TTS "Speaker test, one, two, three"
 *                         through the production fetch+play path.
 */
extern mimi_tool_t audio_spktest_tool;

#ifdef __cplusplus
}
#endif
