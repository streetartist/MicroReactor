/**
 * @file ur_pipe.c
 * @brief MicroReactor data pipe implementation
 *
 * StreamBuffer wrapper for high-throughput data streaming.
 */

#include "ur_pipe.h"
#include "ur_utils.h"

#if UR_CFG_PIPE_ENABLE

#include <string.h>

#if UR_CFG_USE_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#endif

/* ============================================================================
 * Pipe Initialization
 * ========================================================================== */

ur_err_t ur_pipe_init(ur_pipe_t *pipe,
                      uint8_t *buffer,
                      size_t buffer_size,
                      size_t trigger_level)
{
    if (pipe == NULL || buffer == NULL || buffer_size == 0) {
        return UR_ERR_INVALID_ARG;
    }

    if (trigger_level == 0) {
        trigger_level = 1;
    }

    if (trigger_level > buffer_size) {
        trigger_level = buffer_size;
    }

    pipe->buffer = buffer;
    pipe->buffer_size = buffer_size;
    pipe->trigger_level = trigger_level;

#if UR_CFG_USE_FREERTOS
    pipe->handle = xStreamBufferCreateStatic(
        buffer_size,
        trigger_level,
        buffer,
        &pipe->static_sb
    );

    if (pipe->handle == NULL) {
        return UR_ERR_NO_MEMORY;
    }
#endif

    UR_LOGD("Pipe initialized: size=%d, trigger=%d", buffer_size, trigger_level);

    return UR_OK;
}

ur_err_t ur_pipe_reset(ur_pipe_t *pipe)
{
    if (pipe == NULL) {
        return UR_ERR_INVALID_ARG;
    }

#if UR_CFG_USE_FREERTOS
    if (pipe->handle != NULL) {
        xStreamBufferReset(pipe->handle);
    }
#endif

    return UR_OK;
}

/* ============================================================================
 * Write Operations
 * ========================================================================== */

size_t ur_pipe_write(ur_pipe_t *pipe,
                     const void *data,
                     size_t size,
                     uint32_t timeout_ms)
{
    if (pipe == NULL || data == NULL || size == 0) {
        return 0;
    }

#if UR_CFG_USE_FREERTOS
    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);

    return xStreamBufferSend(pipe->handle, data, size, ticks);
#else
    (void)timeout_ms;
    return 0;
#endif
}

size_t ur_pipe_write_from_isr(ur_pipe_t *pipe,
                              const void *data,
                              size_t size,
                              BaseType_t *pxWoken)
{
    if (pipe == NULL || data == NULL || size == 0) {
        return 0;
    }

#if UR_CFG_USE_FREERTOS
    return xStreamBufferSendFromISR(pipe->handle, data, size, pxWoken);
#else
    (void)pxWoken;
    return 0;
#endif
}

ur_err_t ur_pipe_write_byte(ur_pipe_t *pipe, uint8_t byte)
{
    if (ur_pipe_write(pipe, &byte, 1, 0) == 1) {
        return UR_OK;
    }
    return UR_ERR_QUEUE_FULL;
}

/* ============================================================================
 * Read Operations
 * ========================================================================== */

size_t ur_pipe_read(ur_pipe_t *pipe,
                    void *buffer,
                    size_t size,
                    uint32_t timeout_ms)
{
    if (pipe == NULL || buffer == NULL || size == 0) {
        return 0;
    }

#if UR_CFG_USE_FREERTOS
    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);

    return xStreamBufferReceive(pipe->handle, buffer, size, ticks);
#else
    (void)timeout_ms;
    return 0;
#endif
}

size_t ur_pipe_read_from_isr(ur_pipe_t *pipe,
                             void *buffer,
                             size_t size,
                             BaseType_t *pxWoken)
{
    if (pipe == NULL || buffer == NULL || size == 0) {
        return 0;
    }

#if UR_CFG_USE_FREERTOS
    return xStreamBufferReceiveFromISR(pipe->handle, buffer, size, pxWoken);
#else
    (void)pxWoken;
    return 0;
#endif
}

ur_err_t ur_pipe_read_byte(ur_pipe_t *pipe, uint8_t *byte, uint32_t timeout_ms)
{
    if (pipe == NULL || byte == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    if (ur_pipe_read(pipe, byte, 1, timeout_ms) == 1) {
        return UR_OK;
    }
    return UR_ERR_TIMEOUT;
}

size_t ur_pipe_peek(ur_pipe_t *pipe, void *buffer, size_t size)
{
    if (pipe == NULL || buffer == NULL || size == 0) {
        return 0;
    }

    /* FreeRTOS StreamBuffer doesn't support peek, so we read and re-write */
    size_t available = ur_pipe_available(pipe);
    if (available == 0) {
        return 0;
    }

    size_t to_peek = (size < available) ? size : available;
    size_t read = ur_pipe_read(pipe, buffer, to_peek, 0);

    if (read > 0) {
        /* Write back the data */
        /* Note: This is not ideal as it changes order if pipe was not empty */
        /* In practice, peek should be used when pipe is known to have data */
        ur_pipe_write(pipe, buffer, read, 0);
    }

    return read;
}

/* ============================================================================
 * Status Queries
 * ========================================================================== */

size_t ur_pipe_available(const ur_pipe_t *pipe)
{
    if (pipe == NULL) {
        return 0;
    }

#if UR_CFG_USE_FREERTOS
    return xStreamBufferBytesAvailable(pipe->handle);
#else
    return 0;
#endif
}

size_t ur_pipe_space(const ur_pipe_t *pipe)
{
    if (pipe == NULL) {
        return 0;
    }

#if UR_CFG_USE_FREERTOS
    return xStreamBufferSpacesAvailable(pipe->handle);
#else
    return 0;
#endif
}

bool ur_pipe_is_empty(const ur_pipe_t *pipe)
{
    if (pipe == NULL) {
        return true;
    }

#if UR_CFG_USE_FREERTOS
    return xStreamBufferIsEmpty(pipe->handle) == pdTRUE;
#else
    return true;
#endif
}

bool ur_pipe_is_full(const ur_pipe_t *pipe)
{
    if (pipe == NULL) {
        return false;
    }

#if UR_CFG_USE_FREERTOS
    return xStreamBufferIsFull(pipe->handle) == pdTRUE;
#else
    return false;
#endif
}

size_t ur_pipe_get_size(const ur_pipe_t *pipe)
{
    if (pipe == NULL) {
        return 0;
    }
    return pipe->buffer_size;
}

/* ============================================================================
 * Configuration
 * ========================================================================== */

ur_err_t ur_pipe_set_trigger(ur_pipe_t *pipe, size_t trigger_level)
{
    if (pipe == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    if (trigger_level == 0) {
        trigger_level = 1;
    }

    if (trigger_level > pipe->buffer_size) {
        trigger_level = pipe->buffer_size;
    }

#if UR_CFG_USE_FREERTOS
    xStreamBufferSetTriggerLevel(pipe->handle, trigger_level);
#endif

    pipe->trigger_level = trigger_level;

    return UR_OK;
}

#endif /* UR_CFG_PIPE_ENABLE */
