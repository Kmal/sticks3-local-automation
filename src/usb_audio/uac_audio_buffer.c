#include "uac_audio_buffer.h"

#include <string.h>

esp_err_t uac_audio_buffer_init(uac_audio_buffer_t *buffer, uint8_t *storage, size_t capacity)
{
    if (buffer == NULL || storage == NULL || capacity == 0 || capacity > UINT32_MAX / 2u) {
        return ESP_ERR_INVALID_ARG;
    }
    buffer->storage = storage;
    buffer->capacity = capacity;
    atomic_init(&buffer->read_position, 0);
    atomic_init(&buffer->write_position, 0);
    atomic_init(&buffer->underruns, 0);
    atomic_init(&buffer->overruns, 0);
    atomic_init(&buffer->bytes_written, 0);
    atomic_init(&buffer->bytes_read, 0);
    buffer->head = buffer->tail = 0;
    return ESP_OK;
}

void uac_audio_buffer_reset(uac_audio_buffer_t *buffer)
{
    if (buffer == NULL) {
        return;
    }
    buffer->head = 0;
    buffer->tail = 0;
    atomic_store(&buffer->read_position, 0);
    atomic_store(&buffer->write_position, 0);
    buffer->underruns = 0;
    buffer->overruns = 0;
    buffer->bytes_written = 0;
    buffer->bytes_read = 0;
}

size_t uac_audio_buffer_write(uac_audio_buffer_t *buffer, const uint8_t *src, size_t len)
{
    if (buffer == NULL || src == NULL || buffer->storage == NULL || buffer->capacity == 0 || len == 0) {
        return 0;
    }
    const unsigned write = atomic_load_explicit(&buffer->write_position, memory_order_relaxed);
    const unsigned read = atomic_load_explicit(&buffer->read_position, memory_order_acquire);
    size_t written = len < buffer->capacity - (unsigned)(write - read) ? len : buffer->capacity - (unsigned)(write - read);
    const size_t first = written < buffer->capacity - buffer->head ? written : buffer->capacity - buffer->head;
    memcpy(buffer->storage + buffer->head, src, first);
    memcpy(buffer->storage, src + first, written - first);
    buffer->head = (buffer->head + written) % buffer->capacity;
    atomic_store_explicit(&buffer->write_position, write + (unsigned)written, memory_order_release);
    if (written < len) buffer->overruns++;
    buffer->bytes_written += (unsigned)written;
    return written;
}

size_t uac_audio_buffer_read(uac_audio_buffer_t *buffer, uint8_t *dest, size_t len)
{
    if (buffer == NULL || dest == NULL || buffer->storage == NULL || buffer->capacity == 0 || len == 0) {
        return 0;
    }
    const unsigned position = atomic_load_explicit(&buffer->read_position, memory_order_relaxed);
    const unsigned write = atomic_load_explicit(&buffer->write_position, memory_order_acquire);
    size_t read = len < (unsigned)(write - position) ? len : (unsigned)(write - position);
    const size_t first = read < buffer->capacity - buffer->tail ? read : buffer->capacity - buffer->tail;
    memcpy(dest, buffer->storage + buffer->tail, first);
    memcpy(dest + first, buffer->storage, read - first);
    buffer->tail = (buffer->tail + read) % buffer->capacity;
    atomic_store_explicit(&buffer->read_position, position + (unsigned)read, memory_order_release);
    if (read < len) buffer->underruns++;
    buffer->bytes_read += (unsigned)read;
    return read;
}

size_t uac_audio_buffer_read_or_silence(uac_audio_buffer_t *buffer, uint8_t *dest, size_t len)
{
    if (dest == NULL || len == 0) {
        return 0;
    }
    size_t read = uac_audio_buffer_read(buffer, dest, len);
    if (read < len) {
        memset(dest + read, 0, len - read);
    }
    return len;
}

uac_audio_buffer_stats_t uac_audio_buffer_get_stats(const uac_audio_buffer_t *buffer)
{
    if (buffer == NULL) {
        return (uac_audio_buffer_stats_t){0};
    }
    unsigned read = atomic_load(&buffer->read_position);
    unsigned write = atomic_load(&buffer->write_position);
    size_t used = (unsigned)(write - read);
    if (used > buffer->capacity) used = buffer->capacity;
    return (uac_audio_buffer_stats_t){
        .underruns = buffer->underruns,
        .overruns = buffer->overruns,
        .bytes_written = buffer->bytes_written,
        .bytes_read = buffer->bytes_read,
        .used = used,
        .capacity = buffer->capacity,
    };
}
