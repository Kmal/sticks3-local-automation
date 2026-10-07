#include "board_audio_clock.h"

#include "board_sticks3.h"

/*
 * Current StickS3 board-support profile: 16 kHz mono samples with a fixed
 * 12.288 MHz MCLK. For the ESP-IDF standard I2S channel API used by this
 * project, the 768x MCLK multiple produces the documented 12.288 MHz MCLK
 * for 16 kHz audio. Hardware acceptance must measure GPIO18, GPIO17, and
 * GPIO15 before claiming physical audio success.
 */
/* ESP-IDF v6.1 standard mode always clocks two slots, including mono:
 * https://github.com/espressif/esp-idf/blob/v6.1/components/esp_driver_i2s/i2s_std.c */
_Static_assert(BOARD_I2S_BCLK_HZ == BOARD_I2S_SAMPLE_RATE * BOARD_I2S_FRAME_SLOTS * BOARD_I2S_SLOT_BITS,
               "BCLK must match the standard I2S frame");
static const board_audio_clock_profile_t s_profile = {
    .sample_rate_hz = BOARD_I2S_SAMPLE_RATE,
    .mclk_hz = BOARD_I2S_MCLK_HZ,
    .bclk_hz = BOARD_I2S_BCLK_HZ,
    .lrck_hz = BOARD_I2S_LRCK_HZ,
    .bits_per_sample = 16,
    .channels = 1,
    .slot_bits = BOARD_I2S_SLOT_BITS,
    .frame_slots = BOARD_I2S_FRAME_SLOTS,
    .fixed_mclk_authoritative = true,
    .mclk_multiple_for_driver = 768,
    .es8311_clk_reg_value = 0x40,
    .source_note = "StickS3 pin map + current project ES8311 16 kHz clock-manager sequence; measure clocks on hardware",
};

const board_audio_clock_profile_t *board_audio_clock_get_profile(void)
{
    return &s_profile;
}
