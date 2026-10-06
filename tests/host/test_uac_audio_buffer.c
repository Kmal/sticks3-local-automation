#include "uac_audio_buffer.h"

#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_EQ(e,a) do { if ((e)!=(a)) { fprintf(stderr, "%s:%d expected %ld got %ld\n", __FILE__, __LINE__, (long)(e), (long)(a)); exit(1);} } while(0)
#define ASSERT_TRUE(x) do { if (!(x)) { fprintf(stderr, "%s:%d assertion failed: %s\n", __FILE__, __LINE__, #x); exit(1);} } while(0)

static void test_write_read_wrap_and_stats(void)
{
    uint8_t storage[4];
    uac_audio_buffer_t b;
    ASSERT_EQ(ESP_OK, uac_audio_buffer_init(&b, storage, sizeof(storage)));
    uint8_t in[] = {1,2,3};
    ASSERT_EQ(3, uac_audio_buffer_write(&b, in, sizeof(in)));
    uint8_t out[2] = {0};
    ASSERT_EQ(2, uac_audio_buffer_read(&b, out, sizeof(out)));
    ASSERT_EQ(1, out[0]);
    ASSERT_EQ(2, out[1]);
    uint8_t more[] = {4,5,6};
    ASSERT_EQ(3, uac_audio_buffer_write(&b, more, sizeof(more)));
    uint8_t all[4] = {0};
    ASSERT_EQ(4, uac_audio_buffer_read(&b, all, sizeof(all)));
    ASSERT_TRUE(memcmp(all, (uint8_t[]){3,4,5,6}, 4) == 0);
    uac_audio_buffer_stats_t st = uac_audio_buffer_get_stats(&b);
    ASSERT_EQ(6, st.bytes_written);
    ASSERT_EQ(6, st.bytes_read);
}

static void test_overrun_and_silence_underrun(void)
{
    uint8_t storage[2];
    uac_audio_buffer_t b;
    ASSERT_EQ(ESP_OK, uac_audio_buffer_init(&b, storage, sizeof(storage)));
    uint8_t in[] = {9,8,7};
    ASSERT_EQ(2, uac_audio_buffer_write(&b, in, sizeof(in)));
    uint8_t out[4] = {1,1,1,1};
    ASSERT_EQ(4, uac_audio_buffer_read_or_silence(&b, out, sizeof(out)));
    ASSERT_TRUE(memcmp(out, (uint8_t[]){9,8,0,0}, 4) == 0);
    uac_audio_buffer_stats_t st = uac_audio_buffer_get_stats(&b);
    ASSERT_EQ(1, st.overruns);
    ASSERT_EQ(1, st.underruns);
}


#define CONCURRENT_BYTES 1000000u
static uac_audio_buffer_t concurrent_buffer;
static void *producer(void *ctx)
{
    (void)ctx;
    for (unsigned i = 0; i < CONCURRENT_BYTES;) {
        uint8_t byte = (uint8_t)i;
        if (uac_audio_buffer_write(&concurrent_buffer, &byte, 1) == 1) ++i;
        else sched_yield();
    }
    return NULL;
}
static void test_concurrent_pcm_integrity(void)
{
    uint8_t storage[127];
    ASSERT_EQ(ESP_OK, uac_audio_buffer_init(&concurrent_buffer, storage, sizeof(storage)));
    /* Exercise position overflow independently of non-power-of-two storage. */
    atomic_store(&concurrent_buffer.read_position, UINT32_MAX - 99u);
    atomic_store(&concurrent_buffer.write_position, UINT32_MAX - 99u);
    pthread_t thread;
    ASSERT_EQ(0, pthread_create(&thread, NULL, producer, NULL));
    for (unsigned i = 0; i < CONCURRENT_BYTES;) {
        uint8_t byte;
        if (uac_audio_buffer_read(&concurrent_buffer, &byte, 1) == 1) {
            ASSERT_EQ((uint8_t)i, byte);
            ++i;
        } else sched_yield();
    }
    ASSERT_EQ(0, pthread_join(thread, NULL));
    uac_audio_buffer_stats_t stats = uac_audio_buffer_get_stats(&concurrent_buffer);
    ASSERT_EQ(CONCURRENT_BYTES, stats.bytes_read);
    ASSERT_EQ(CONCURRENT_BYTES, stats.bytes_written);
    ASSERT_EQ(0, stats.used);
}

int main(void)
{
    test_concurrent_pcm_integrity();
    test_write_read_wrap_and_stats();
    test_overrun_and_silence_underrun();
    puts("uac_audio_buffer tests passed");
    return 0;
}
