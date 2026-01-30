/**
 * @file ur_pipe.h
 * @brief MicroReactor data pipe API
 *
 * StreamBuffer wrapper for high-throughput data streaming.
 * Zero dynamic allocation - uses static buffers.
 */

#ifndef UR_PIPE_H
#define UR_PIPE_H

#include "ur_types.h"

#if UR_CFG_PIPE_ENABLE

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Pipe Initialization
 * ========================================================================== */

/**
 * @brief Initialize a data pipe with static buffer
 *
 * @param pipe          Pipe structure (must be static)
 * @param buffer        Data buffer (must be static)
 * @param buffer_size   Buffer size in bytes
 * @param trigger_level Minimum bytes for blocking read to unblock (1 for byte-by-byte)
 * @return UR_OK on success
 */
ur_err_t ur_pipe_init(ur_pipe_t *pipe,
                      uint8_t *buffer,
                      size_t buffer_size,
                      size_t trigger_level);

/**
 * @brief Reset pipe to empty state
 *
 * @param pipe  Pipe to reset
 * @return UR_OK on success
 */
ur_err_t ur_pipe_reset(ur_pipe_t *pipe);

/* ============================================================================
 * Pipe Write Operations
 * ========================================================================== */

/**
 * @brief Write data to pipe
 *
 * @param pipe          Pipe to write to
 * @param data          Data buffer
 * @param size          Number of bytes to write
 * @param timeout_ms    Timeout in milliseconds (0 = no wait)
 * @return Number of bytes written
 */
size_t ur_pipe_write(ur_pipe_t *pipe,
                     const void *data,
                     size_t size,
                     uint32_t timeout_ms);

/**
 * @brief Write data to pipe from ISR
 *
 * @param pipe      Pipe to write to
 * @param data      Data buffer
 * @param size      Number of bytes to write
 * @param pxWoken   Set to pdTRUE if higher priority task was woken
 * @return Number of bytes written
 */
size_t ur_pipe_write_from_isr(ur_pipe_t *pipe,
                              const void *data,
                              size_t size,
                              BaseType_t *pxWoken);

/**
 * @brief Write single byte to pipe
 *
 * @param pipe  Pipe to write to
 * @param byte  Byte to write
 * @return UR_OK on success, UR_ERR_QUEUE_FULL if full
 */
ur_err_t ur_pipe_write_byte(ur_pipe_t *pipe, uint8_t byte);

/* ============================================================================
 * Pipe Read Operations
 * ========================================================================== */

/**
 * @brief Read data from pipe
 *
 * @param pipe          Pipe to read from
 * @param buffer        Buffer to read into
 * @param size          Maximum bytes to read
 * @param timeout_ms    Timeout in milliseconds (0 = no wait)
 * @return Number of bytes read
 */
size_t ur_pipe_read(ur_pipe_t *pipe,
                    void *buffer,
                    size_t size,
                    uint32_t timeout_ms);

/**
 * @brief Read data from pipe in ISR
 *
 * @param pipe      Pipe to read from
 * @param buffer    Buffer to read into
 * @param size      Maximum bytes to read
 * @param pxWoken   Set to pdTRUE if higher priority task was woken
 * @return Number of bytes read
 */
size_t ur_pipe_read_from_isr(ur_pipe_t *pipe,
                             void *buffer,
                             size_t size,
                             BaseType_t *pxWoken);

/**
 * @brief Read single byte from pipe
 *
 * @param pipe      Pipe to read from
 * @param byte      Pointer to store byte
 * @param timeout_ms Timeout in milliseconds
 * @return UR_OK on success, UR_ERR_TIMEOUT if empty
 */
ur_err_t ur_pipe_read_byte(ur_pipe_t *pipe, uint8_t *byte, uint32_t timeout_ms);

/**
 * @brief Peek at data without removing
 *
 * Note: FreeRTOS StreamBuffer doesn't support peek, so this reads
 * and re-writes the data. Use sparingly.
 *
 * @param pipe      Pipe to peek
 * @param buffer    Buffer to read into
 * @param size      Maximum bytes to peek
 * @return Number of bytes peeked
 */
size_t ur_pipe_peek(ur_pipe_t *pipe, void *buffer, size_t size);

/* ============================================================================
 * Pipe Status Queries
 * ========================================================================== */

/**
 * @brief Get number of bytes available to read
 *
 * @param pipe  Pipe to query
 * @return Number of bytes in pipe
 */
size_t ur_pipe_available(const ur_pipe_t *pipe);

/**
 * @brief Get available space for writing
 *
 * @param pipe  Pipe to query
 * @return Number of bytes that can be written
 */
size_t ur_pipe_space(const ur_pipe_t *pipe);

/**
 * @brief Check if pipe is empty
 *
 * @param pipe  Pipe to check
 * @return true if empty
 */
bool ur_pipe_is_empty(const ur_pipe_t *pipe);

/**
 * @brief Check if pipe is full
 *
 * @param pipe  Pipe to check
 * @return true if full
 */
bool ur_pipe_is_full(const ur_pipe_t *pipe);

/**
 * @brief Get pipe buffer size
 *
 * @param pipe  Pipe to query
 * @return Total buffer size
 */
size_t ur_pipe_get_size(const ur_pipe_t *pipe);

/* ============================================================================
 * Pipe Configuration
 * ========================================================================== */

/**
 * @brief Set trigger level for blocking reads
 *
 * @param pipe          Pipe to configure
 * @param trigger_level New trigger level
 * @return UR_OK on success
 */
ur_err_t ur_pipe_set_trigger(ur_pipe_t *pipe, size_t trigger_level);

/* ============================================================================
 * Convenience Macros
 * ========================================================================== */

/**
 * @brief Declare and initialize a static pipe
 *
 * Usage:
 *   UR_PIPE_DEFINE(my_pipe, 256, 1);
 */
#define UR_PIPE_DEFINE(name, size, trigger) \
    static uint8_t name##_buffer[size]; \
    static ur_pipe_t name = { \
        .buffer = name##_buffer, \
        .buffer_size = (size), \
        .trigger_level = (trigger) \
    }

/**
 * @brief Initialize a statically declared pipe
 *
 * Call this at startup for pipes declared with UR_PIPE_DEFINE.
 */
#define UR_PIPE_INIT(name) \
    ur_pipe_init(&(name), (name##_buffer), sizeof(name##_buffer), (name).trigger_level)

#ifdef __cplusplus
}
#endif

#endif /* UR_CFG_PIPE_ENABLE */

#endif /* UR_PIPE_H */
