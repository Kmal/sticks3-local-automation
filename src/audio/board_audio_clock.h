#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2s_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int sample_rate_hz;
    int mclk_hz;
    int bclk_hz;
    int lrck_hz;
    int bits_per_sample;
    int channels;
    int slot_bits;
    int frame_slots;
    bool fixed_mclk_authoritative;
    int mclk_multiple_for_driver;
    uint8_t es8311_clk_reg_value;
    const char *source_note;
} board_audio_clock_profile_t;

const board_audio_clock_profile_t *board_audio_clock_get_profile(void);

#ifdef __cplusplus
}
#endif
