/*
 * voice_client.h — hands-free voice pipeline (WakeNet wake word + backend STT/TTS)
 *
 * Audio format contract with the backend (api/voice_router.py):
 *   → STT: raw 16 kHz mono 16-bit PCM  (POST /api/voice/transcribe)
 *   ← TTS: WAV 16 kHz mono 16-bit PCM  (POST /api/voice/tts)
 *
 * The ESP32 only moves bytes — Whisper and edge-tts run on the backend,
 * and wake-word detection (esp-sr WakeNet, "Hi ESP") runs on-chip.
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the wake-word listener ("Hi ESP" → Yes? → command → reply).
 *
 * Loads the WakeNet model from the "model" partition, pre-caches the
 * "Yes?" ack WAV from the backend, and spawns the listener task.
 * Call once from app_main after WiFi is up (ack fetch needs the network;
 * if it fails the ack falls back to a beep and retries are not needed).
 */
esp_err_t voice_client_start(void);

/**
 * @brief Stop the wake listener (task self-deletes on the next tick).
 */
void voice_client_stop(void);

/**
 * @brief Enable or disable autonomous VAD wake-word listening at runtime.
 */
void voice_client_set_listen(bool enable);
bool voice_client_is_listen_enabled(void);

/**
 * @brief Speak a reply: fetch TTS audio from the backend and play it.
 *
 * Called by the outbound dispatcher for system:"voice" replies. Blocking
 * for the duration of fetch+playback (runs on the dispatcher's context);
 * returns false if the pipeline is already busy.
 */
bool voice_client_speak(const char *text);

/**
 * @brief Speak text while showing explicit OLED lines instead of auto-splitting
 *        the spoken text. Useful for event alerts like Owner Detected.
 */
bool voice_client_speak_with_display(const char *text, const char *line1, const char *line2);

/**
 * @brief Structured STT result including wake word match status.
 */
typedef struct {
    bool  wake_matched;  /* true if transcript began with a valid wake word */
    char *text;          /* command text with wake word stripped (caller frees) */
    char *raw_text;      /* full unmodified transcript (caller frees) */
    char *wake_word;     /* matched wake word string (caller frees) */
} voice_stt_result_t;

/**
 * @brief Full STT call returning wake word detection and command text.
 */
bool voice_client_transcribe_full(const int16_t *pcm, size_t samples, voice_stt_result_t *out_res);

/**
 * @brief Transcribe raw 16kHz mono PCM via backend STT (/api/voice/transcribe).
 * Returns heap-allocated string (caller frees), or NULL on failure.
 */
char *voice_client_transcribe(const int16_t *pcm, size_t samples);

/**
 * @brief Multimodal vision query sending camera JPEG + optional query to backend (/api/camera/vision-query).
 * Returns heap-allocated response string (caller frees), or NULL on failure.
 */
char *voice_client_vision_query(const uint8_t *jpeg_data, size_t jpeg_len, const char *query);

/**
 * @brief Multimodal vision query returning both full reply and OLED distilled lines.
 */
char *voice_client_vision_query_full(const uint8_t *jpeg_data, size_t jpeg_len, const char *query,
                                     char *out_l1, size_t l1_sz, char *out_l2, size_t l2_sz);

#ifdef __cplusplus
}
#endif
