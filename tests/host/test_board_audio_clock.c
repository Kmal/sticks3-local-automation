#include "board_audio_clock.h"
#include <stdio.h>
#include <stdlib.h>

#define ASSERT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d assertion failed: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
#define ASSERT_EQ(expected, actual) do { if ((expected) != (actual)) { fprintf(stderr, "%s:%d expected %ld got %ld\n", __FILE__, __LINE__, (long)(expected), (long)(actual)); exit(1); } } while (0)

int main(void)
{
    const board_audio_clock_profile_t *profile = board_audio_clock_get_profile();
    ASSERT_EQ(16000, profile->sample_rate_hz);
    ASSERT_EQ(12288000, profile->mclk_hz);
    ASSERT_EQ(1024000, profile->bclk_hz);
    ASSERT_EQ(2, profile->frame_slots);
    ASSERT_EQ(32, profile->slot_bits);
    ASSERT_EQ(profile->bclk_hz, profile->sample_rate_hz * profile->frame_slots * profile->slot_bits);
    ASSERT_TRUE(profile->fixed_mclk_authoritative);
    ASSERT_EQ(768, profile->mclk_multiple_for_driver);
    ASSERT_EQ(0x40, profile->es8311_clk_reg_value);
    ASSERT_TRUE(profile->source_note != NULL);
    puts("board_audio_clock tests passed");
    return 0;
}
