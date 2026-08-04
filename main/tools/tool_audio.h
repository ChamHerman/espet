#pragma once

#include "esp_err.h"
#include "tools/tool_registry.h"

#define I2S_SHARED_BCLK_IO  41   /* Shared Bit Clock (SCK / BCLK) */
#define I2S_SHARED_WS_IO    42   /* Shared Word Select (WS / LRC) */
#define I2S_AMP_DIN_IO      2    /* Speaker Data Output (DIN) */
#define I2S_MIC_SD_IO       1    /* Microphone Data Input (SD) */

#define I2S_SAMPLE_RATE     16000

/**
 * Initialize I2S audio driver for shared mic + speaker.
 */
esp_err_t tool_audio_init(void);

/**
 * Start/stop Push-To-Talk (PTT) mic recording buffer.
 */
void tool_audio_ptt_record_start(void);
void tool_audio_ptt_record_stop(void);

extern mimi_tool_t audio_play_tone_tool;
extern mimi_tool_t audio_record_test_tool;
