/**
 * @file ur_power.h
 * @brief MicroReactor Power Management Framework
 *
 * Vote-based automatic low-power management:
 * - Entities declare their power requirements via locks
 * - Framework calculates minimum allowed sleep level
 * - Integrates with dispatch loop for tickless idle
 * - HAL abstraction for platform-specific sleep modes
 *
 * Usage:
 * 1. Define power modes with ur_power_mode_t
 * 2. Entities lock/unlock power modes as needed
 * 3. Call ur_power_idle() in main loop when no signals pending
 */

#ifndef UR_POWER_H
#define UR_POWER_H

#include "ur_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration
 * ========================================================================== */

#ifndef UR_CFG_POWER_ENABLE
#ifdef CONFIG_UR_POWER_ENABLE
#define UR_CFG_POWER_ENABLE         CONFIG_UR_POWER_ENABLE
#else
#define UR_CFG_POWER_ENABLE         1
#endif
#endif

#ifndef UR_CFG_POWER_MAX_MODES
#ifdef CONFIG_UR_POWER_MAX_MODES
#define UR_CFG_POWER_MAX_MODES      CONFIG_UR_POWER_MAX_MODES
#else
#define UR_CFG_POWER_MAX_MODES      4
#endif
#endif

#ifndef UR_CFG_POWER_IDLE_THRESHOLD_MS
#ifdef CONFIG_UR_POWER_IDLE_THRESHOLD_MS
#define UR_CFG_POWER_IDLE_THRESHOLD_MS CONFIG_UR_POWER_IDLE_THRESHOLD_MS
#else
#define UR_CFG_POWER_IDLE_THRESHOLD_MS 100
#endif
#endif

#if UR_CFG_POWER_ENABLE

/* ============================================================================
 * Types
 * ========================================================================== */

/**
 * @brief Power modes (lower value = more power consumption)
 *
 * The framework will enter the highest mode that all locks allow.
 */
typedef enum {
    UR_POWER_ACTIVE = 0,        /**< Full power, CPU running */
    UR_POWER_IDLE,              /**< CPU idle, peripherals active */
    UR_POWER_LIGHT_SLEEP,       /**< Light sleep, fast wakeup */
    UR_POWER_DEEP_SLEEP,        /**< Deep sleep, slow wakeup */
    UR_POWER_MODE_COUNT         /**< Number of modes */
} ur_power_mode_t;

/**
 * @brief Power lock entry
 */
typedef struct {
    uint16_t entity_id;         /**< Entity holding the lock */
    uint8_t mode;               /**< Mode being locked (prevented) */
    uint8_t count;              /**< Lock count (for nested locks) */
} ur_power_lock_t;

/**
 * @brief Wakeup source flags
 */
typedef enum {
    UR_WAKE_NONE = 0x00,
    UR_WAKE_TIMER = 0x01,       /**< Timer/RTC wakeup */
    UR_WAKE_GPIO = 0x02,        /**< GPIO interrupt */
    UR_WAKE_UART = 0x04,        /**< UART activity */
    UR_WAKE_TOUCH = 0x08,       /**< Touch sensor */
    UR_WAKE_ALL = 0xFF,         /**< All sources */
} ur_wake_source_t;

/**
 * @brief Power statistics
 */
typedef struct {
    uint32_t active_time_ms;    /**< Time in active mode */
    uint32_t idle_time_ms;      /**< Time in idle mode */
    uint32_t light_sleep_ms;    /**< Time in light sleep */
    uint32_t deep_sleep_ms;     /**< Time in deep sleep */
    uint32_t wakeup_count;      /**< Number of wakeups */
    uint32_t last_wakeup_reason;/**< Last wakeup source */
} ur_power_stats_t;

/**
 * @brief HAL callbacks for platform-specific sleep implementation
 */
typedef struct {
    /**
     * @brief Enter idle mode (CPU stopped, peripherals running)
     * @param timeout_ms Maximum idle time (0 = indefinite)
     */
    void (*enter_idle)(uint32_t timeout_ms);

    /**
     * @brief Enter light sleep mode
     * @param timeout_ms Maximum sleep time
     * @param wake_sources Allowed wakeup sources
     */
    void (*enter_light_sleep)(uint32_t timeout_ms, uint8_t wake_sources);

    /**
     * @brief Enter deep sleep mode
     * @param timeout_ms Maximum sleep time (0 = indefinite)
     * @param wake_sources Allowed wakeup sources
     */
    void (*enter_deep_sleep)(uint32_t timeout_ms, uint8_t wake_sources);

    /**
     * @brief Get last wakeup reason
     * @return Wakeup source that caused last wake
     */
    ur_wake_source_t (*get_wakeup_reason)(void);

    /**
     * @brief Get current time in milliseconds
     * @return Current timestamp
     */
    uint32_t (*get_time_ms)(void);
} ur_power_hal_t;

/* ============================================================================
 * Initialization
 * ========================================================================== */

/**
 * @brief Initialize power management system
 *
 * @param hal   HAL callbacks (NULL for default/no-op)
 * @return UR_OK on success
 */
ur_err_t ur_power_init(const ur_power_hal_t *hal);

/* ============================================================================
 * Lock Management
 * ========================================================================== */

/**
 * @brief Acquire a power lock
 *
 * Prevents the system from entering the specified mode or deeper.
 * Locks are reference-counted (can lock multiple times).
 *
 * @param ent   Entity acquiring the lock
 * @param mode  Power mode to prevent
 * @return UR_OK on success
 *
 * @code
 * // Audio entity prevents sleep while playing
 * ur_power_lock(&audio_ent, UR_POWER_LIGHT_SLEEP);
 * play_audio();
 * ur_power_unlock(&audio_ent, UR_POWER_LIGHT_SLEEP);
 * @endcode
 */
ur_err_t ur_power_lock(ur_entity_t *ent, ur_power_mode_t mode);

/**
 * @brief Release a power lock
 *
 * @param ent   Entity releasing the lock
 * @param mode  Power mode being released
 * @return UR_OK on success, UR_ERR_NOT_FOUND if not locked
 */
ur_err_t ur_power_unlock(ur_entity_t *ent, ur_power_mode_t mode);

/**
 * @brief Release all locks held by an entity
 *
 * Called automatically when entity stops.
 *
 * @param ent   Entity
 * @return Number of locks released
 */
int ur_power_unlock_all(ur_entity_t *ent);

/**
 * @brief Check if a mode is currently locked
 *
 * @param mode  Power mode to check
 * @return true if any entity has locked this mode
 */
bool ur_power_is_locked(ur_power_mode_t mode);

/* ============================================================================
 * Sleep Control
 * ========================================================================== */

/**
 * @brief Check if system can enter sleep mode
 *
 * Returns the deepest sleep mode allowed by current locks.
 *
 * @return Deepest allowed power mode
 */
ur_power_mode_t ur_power_get_allowed_mode(void);

/**
 * @brief Enter idle/sleep mode
 *
 * Called from main loop when no signals are pending.
 * Automatically selects appropriate sleep level based on locks.
 *
 * @param timeout_ms Maximum sleep time (0 = use next event time)
 * @return Actual time slept in milliseconds
 *
 * @code
 * void app_main(void) {
 *     while (1) {
 *         int processed = ur_dispatch_multi(entities, count);
 *         if (processed == 0) {
 *             ur_power_idle(ur_power_get_next_event_ms());
 *         }
 *     }
 * }
 * @endcode
 */
uint32_t ur_power_idle(uint32_t timeout_ms);

/**
 * @brief Force entry to specific power mode
 *
 * Bypasses lock checks. Use with caution.
 *
 * @param mode      Power mode to enter
 * @param timeout_ms Maximum time in mode
 * @param wake_sources Allowed wakeup sources
 * @return Actual time in mode
 */
uint32_t ur_power_enter_mode(ur_power_mode_t mode,
                              uint32_t timeout_ms,
                              uint8_t wake_sources);

/* ============================================================================
 * Event Time Management
 * ========================================================================== */

/**
 * @brief Register next expected event time
 *
 * Used for tickless idle calculation.
 *
 * @param ent       Entity
 * @param time_ms   Absolute time of next event (0 = no pending events)
 */
void ur_power_set_next_event(ur_entity_t *ent, uint32_t time_ms);

/**
 * @brief Get time until next scheduled event
 *
 * @return Milliseconds until next event, UINT32_MAX if none
 */
uint32_t ur_power_get_next_event_ms(void);

/* ============================================================================
 * Statistics
 * ========================================================================== */

/**
 * @brief Get power statistics
 *
 * @param stats Output statistics structure
 */
void ur_power_get_stats(ur_power_stats_t *stats);

/**
 * @brief Reset power statistics
 */
void ur_power_reset_stats(void);

/**
 * @brief Get current power mode name (for logging)
 *
 * @param mode  Power mode
 * @return Mode name string
 */
const char *ur_power_mode_name(ur_power_mode_t mode);

/* ============================================================================
 * Debug
 * ========================================================================== */

/**
 * @brief Print power management state
 */
void ur_power_dump(void);

/* ============================================================================
 * Built-in HAL Implementations
 * ========================================================================== */

#if defined(CONFIG_IDF_TARGET) || defined(ESP_PLATFORM)
/**
 * @brief ESP-IDF power management HAL
 */
extern const ur_power_hal_t ur_power_hal_esp;
#endif

/**
 * @brief No-op HAL (for testing)
 */
extern const ur_power_hal_t ur_power_hal_noop;

#endif /* UR_CFG_POWER_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* UR_POWER_H */
