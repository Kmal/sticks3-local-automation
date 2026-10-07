#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *storage;
    size_t capacity;
    size_t head;
    size_t tail;
    atomic_uint read_position;
    atomic_uint write_position;
    atomic_uint underruns;
    atomic_uint overruns;
    atomic_uint bytes_written;
    atomic_uint bytes_read;
} uac_audio_buffer_t;

typedef struct {
    uint32_t underruns;
    uint32_t overruns;
    uint32_t bytes_written;
    uint32_t bytes_read;
    size_t used;
    size_t capacity;
} uac_audio_buffer_stats_t;

/* One producer and one consumer per ring. Reset only while both are stopped. */
esp_err_t uac_audio_buffer_init(uac_audio_buffer_t *buffer, uint8_t *storage, size_t capacity);
void uac_audio_buffer_reset(uac_audio_buffer_t *buffer);
size_t uac_audio_buffer_write(uac_audio_buffer_t *buffer, const uint8_t *src, size_t len);
size_t uac_audio_buffer_read(uac_audio_buffer_t *buffer, uint8_t *dest, size_t len);
size_t uac_audio_buffer_read_or_silence(uac_audio_buffer_t *buffer, uint8_t *dest, size_t len);
uac_audio_buffer_stats_t uac_audio_buffer_get_stats(const uac_audio_buffer_t *buffer);

#ifdef __cplusplus
}
#endif
