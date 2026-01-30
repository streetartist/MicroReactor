/**
 * @file ur_utils.h
 * @brief MicroReactor utility functions
 *
 * CRC8, timestamps, debug macros, and other helpers.
 */

#ifndef UR_UTILS_H
#define UR_UTILS_H

#include "ur_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CRC8 Calculation (for Wormhole protocol)
 * ========================================================================== */

/**
 * @brief Calculate CRC8 checksum
 *
 * Uses polynomial 0x07 (CRC-8-CCITT).
 *
 * @param data  Data buffer
 * @param len   Data length
 * @return CRC8 value
 */
uint8_t ur_crc8(const uint8_t *data, size_t len);

/**
 * @brief Update running CRC8 with new byte
 *
 * @param crc   Current CRC value
 * @param byte  New byte
 * @return Updated CRC
 */
uint8_t ur_crc8_update(uint8_t crc, uint8_t byte);

/* ============================================================================
 * Timestamp Utilities
 * ========================================================================== */

/**
 * @brief Get current time in milliseconds
 *
 * Uses esp_timer_get_time() / 1000.
 *
 * @return Time in milliseconds (wraps at ~49 days)
 */
uint32_t ur_time_ms(void);

/**
 * @brief Get current time in microseconds
 *
 * Uses esp_timer_get_time().
 *
 * @return Time in microseconds (64-bit)
 */
uint64_t ur_time_us(void);

/**
 * @brief Check if a timestamp has elapsed
 *
 * Handles wrap-around correctly.
 *
 * @param start     Start timestamp (ms)
 * @param duration  Duration to check (ms)
 * @return true if duration has elapsed since start
 */
bool ur_time_elapsed(uint32_t start, uint32_t duration);

/**
 * @brief Calculate time difference (handles wrap-around)
 *
 * @param start Start timestamp (ms)
 * @param end   End timestamp (ms)
 * @return Difference in milliseconds
 */
uint32_t ur_time_diff(uint32_t start, uint32_t end);

/* ============================================================================
 * Signal Utilities
 * ========================================================================== */

/**
 * @brief Create signal with current timestamp
 *
 * @param id        Signal ID
 * @param src_id    Source entity ID
 * @return Initialized signal
 */
ur_signal_t ur_signal_create(uint16_t id, uint16_t src_id);

/**
 * @brief Create signal with u32 payload
 *
 * @param id        Signal ID
 * @param src_id    Source entity ID
 * @param payload   32-bit payload value
 * @return Initialized signal
 */
ur_signal_t ur_signal_create_u32(uint16_t id, uint16_t src_id, uint32_t payload);

/**
 * @brief Create signal with pointer payload
 *
 * @param id        Signal ID
 * @param src_id    Source entity ID
 * @param ptr       Pointer payload
 * @return Initialized signal
 */
ur_signal_t ur_signal_create_ptr(uint16_t id, uint16_t src_id, void *ptr);

/**
 * @brief Copy a signal
 *
 * @param dst   Destination signal
 * @param src   Source signal
 */
void ur_signal_copy(ur_signal_t *dst, const ur_signal_t *src);

/* ============================================================================
 * Entity Utilities
 * ========================================================================== */

/**
 * @brief Get entity name (or "unnamed" if NULL)
 *
 * @param ent   Entity
 * @return Entity name string
 */
const char *ur_entity_name(const ur_entity_t *ent);

/**
 * @brief Get state name by ID (requires custom implementation)
 *
 * Override this weak function to provide state names.
 *
 * @param ent       Entity
 * @param state_id  State ID
 * @return State name string
 */
const char *ur_state_name(const ur_entity_t *ent, uint16_t state_id);

/**
 * @brief Get signal name by ID (requires custom implementation)
 *
 * Override this weak function to provide signal names.
 *
 * @param signal_id Signal ID
 * @return Signal name string
 */
const char *ur_signal_name(uint16_t signal_id);

/* ============================================================================
 * Memory Utilities
 * ========================================================================== */

/**
 * @brief Zero-fill memory (for scratchpad etc.)
 *
 * @param ptr   Memory pointer
 * @param size  Size in bytes
 */
void ur_memzero(void *ptr, size_t size);

/**
 * @brief Copy memory
 *
 * @param dst   Destination
 * @param src   Source
 * @param size  Size in bytes
 */
void ur_memcpy(void *dst, const void *src, size_t size);

/* ============================================================================
 * Debug Logging Macros
 * ========================================================================== */

#if UR_CFG_ENABLE_LOGGING

#include "esp_log.h"

#define UR_TAG "ur"

#define UR_LOGE(...) ESP_LOGE(UR_TAG, __VA_ARGS__)
#define UR_LOGW(...) ESP_LOGW(UR_TAG, __VA_ARGS__)
#define UR_LOGI(...) ESP_LOGI(UR_TAG, __VA_ARGS__)
#define UR_LOGD(...) ESP_LOGD(UR_TAG, __VA_ARGS__)
#define UR_LOGV(...) ESP_LOGV(UR_TAG, __VA_ARGS__)

#define UR_LOG_SIGNAL(ent, sig) \
    UR_LOGD("Entity[%s] <- Signal[0x%04X] from %d", \
            ur_entity_name(ent), (sig)->id, (sig)->src_id)

#define UR_LOG_TRANSITION(ent, from, to) \
    UR_LOGI("Entity[%s]: State %d -> %d", ur_entity_name(ent), (from), (to))

#else /* !UR_CFG_ENABLE_LOGGING */

#define UR_LOGE(...) ((void)0)
#define UR_LOGW(...) ((void)0)
#define UR_LOGI(...) ((void)0)
#define UR_LOGD(...) ((void)0)
#define UR_LOGV(...) ((void)0)
#define UR_LOG_SIGNAL(ent, sig) ((void)0)
#define UR_LOG_TRANSITION(ent, from, to) ((void)0)

#endif /* UR_CFG_ENABLE_LOGGING */

/* ============================================================================
 * Assertion Macros
 * ========================================================================== */

#if UR_CFG_ENABLE_LOGGING

#define UR_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            UR_LOGE("ASSERT FAILED: %s at %s:%d", #cond, __FILE__, __LINE__); \
            abort(); \
        } \
    } while (0)

#define UR_ASSERT_MSG(cond, msg) \
    do { \
        if (!(cond)) { \
            UR_LOGE("ASSERT FAILED: %s - %s at %s:%d", #cond, msg, __FILE__, __LINE__); \
            abort(); \
        } \
    } while (0)

#else

#define UR_ASSERT(cond) ((void)(cond))
#define UR_ASSERT_MSG(cond, msg) ((void)(cond))

#endif

/* ============================================================================
 * Bit Manipulation Macros
 * ========================================================================== */

#define UR_BIT(n)               (1U << (n))
#define UR_BIT_SET(x, b)        ((x) |= UR_BIT(b))
#define UR_BIT_CLR(x, b)        ((x) &= ~UR_BIT(b))
#define UR_BIT_TST(x, b)        (((x) & UR_BIT(b)) != 0)
#define UR_BIT_TGL(x, b)        ((x) ^= UR_BIT(b))

/* ============================================================================
 * Array Size Macro
 * ========================================================================== */

#define UR_ARRAY_SIZE(arr)      (sizeof(arr) / sizeof((arr)[0]))

/* ============================================================================
 * Min/Max Macros
 * ========================================================================== */

#define UR_MIN(a, b)            (((a) < (b)) ? (a) : (b))
#define UR_MAX(a, b)            (((a) > (b)) ? (a) : (b))
#define UR_CLAMP(x, lo, hi)     UR_MIN(UR_MAX(x, lo), hi)

/* ============================================================================
 * Unused Parameter Macro
 * ========================================================================== */

#define UR_UNUSED(x)            ((void)(x))

#ifdef __cplusplus
}
#endif

#endif /* UR_UTILS_H */
