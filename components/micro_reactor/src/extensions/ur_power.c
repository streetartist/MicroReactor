/**
 * @file ur_power.c
 * @brief MicroReactor Power Management Implementation
 */

#include "ur_power.h"
#include "ur_core.h"
#include "ur_utils.h"
#include <string.h>

#if UR_CFG_POWER_ENABLE

/* ============================================================================
 * Internal State
 * ========================================================================== */

#define MAX_LOCKS (UR_CFG_MAX_ENTITIES * UR_CFG_POWER_MAX_MODES)

typedef struct {
    uint16_t entity_id;
    uint8_t mode;
    uint8_t count;
} lock_entry_t;

static struct {
    lock_entry_t locks[MAX_LOCKS];
    size_t lock_count;
    uint32_t next_events[UR_CFG_MAX_ENTITIES];
    const ur_power_hal_t *hal;
    ur_power_stats_t stats;
    bool initialized;
} g_power = { 0 };

/* ============================================================================
 * Internal Functions
 * ========================================================================== */

static lock_entry_t *find_lock(uint16_t entity_id, ur_power_mode_t mode)
{
    for (size_t i = 0; i < g_power.lock_count; i++) {
        if (g_power.locks[i].entity_id == entity_id &&
            g_power.locks[i].mode == mode) {
            return &g_power.locks[i];
        }
    }
    return NULL;
}

static uint32_t get_time(void)
{
    if (g_power.hal != NULL && g_power.hal->get_time_ms != NULL) {
        return g_power.hal->get_time_ms();
    }
    return ur_get_time_ms();
}

static void track_time(ur_power_mode_t mode, uint32_t duration_ms)
{
    switch (mode) {
        case UR_POWER_ACTIVE:
            g_power.stats.active_time_ms += duration_ms;
            break;
        case UR_POWER_IDLE:
            g_power.stats.idle_time_ms += duration_ms;
            break;
        case UR_POWER_LIGHT_SLEEP:
            g_power.stats.light_sleep_ms += duration_ms;
            break;
        case UR_POWER_DEEP_SLEEP:
            g_power.stats.deep_sleep_ms += duration_ms;
            break;
        default:
            break;
    }
}

/* ============================================================================
 * Initialization
 * ========================================================================== */

ur_err_t ur_power_init(const ur_power_hal_t *hal)
{
    memset(&g_power, 0, sizeof(g_power));
    g_power.hal = hal;
    g_power.initialized = true;

    /* Initialize next_events to "no pending event" */
    for (size_t i = 0; i < UR_CFG_MAX_ENTITIES; i++) {
        g_power.next_events[i] = UINT32_MAX;
    }

    UR_LOGD("Power: initialized");

    return UR_OK;
}

/* ============================================================================
 * Lock Management
 * ========================================================================== */

ur_err_t ur_power_lock(ur_entity_t *ent, ur_power_mode_t mode)
{
    if (ent == NULL || mode >= UR_POWER_MODE_COUNT) {
        return UR_ERR_INVALID_ARG;
    }

    if (!g_power.initialized) {
        ur_power_init(NULL);
    }

    /* Check if already locked */
    lock_entry_t *existing = find_lock(ent->id, mode);
    if (existing != NULL) {
        existing->count++;
        UR_LOGV("Power: Entity[%s] incremented lock on %s (count=%d)",
                ur_entity_name(ent), ur_power_mode_name(mode), existing->count);
        return UR_OK;
    }

    /* Create new lock */
    if (g_power.lock_count >= MAX_LOCKS) {
        UR_LOGW("Power: max locks reached");
        return UR_ERR_NO_MEMORY;
    }

    lock_entry_t *lock = &g_power.locks[g_power.lock_count++];
    lock->entity_id = ent->id;
    lock->mode = mode;
    lock->count = 1;

    UR_LOGD("Power: Entity[%s] locked %s",
            ur_entity_name(ent), ur_power_mode_name(mode));

    return UR_OK;
}

ur_err_t ur_power_unlock(ur_entity_t *ent, ur_power_mode_t mode)
{
    if (ent == NULL || mode >= UR_POWER_MODE_COUNT) {
        return UR_ERR_INVALID_ARG;
    }

    lock_entry_t *lock = find_lock(ent->id, mode);
    if (lock == NULL) {
        return UR_ERR_NOT_FOUND;
    }

    lock->count--;

    if (lock->count == 0) {
        /* Remove lock by shifting */
        size_t idx = lock - g_power.locks;
        for (size_t i = idx; i < g_power.lock_count - 1; i++) {
            g_power.locks[i] = g_power.locks[i + 1];
        }
        g_power.lock_count--;

        UR_LOGD("Power: Entity[%s] unlocked %s",
                ur_entity_name(ent), ur_power_mode_name(mode));
    } else {
        UR_LOGV("Power: Entity[%s] decremented lock on %s (count=%d)",
                ur_entity_name(ent), ur_power_mode_name(mode), lock->count);
    }

    return UR_OK;
}

int ur_power_unlock_all(ur_entity_t *ent)
{
    if (ent == NULL) {
        return 0;
    }

    int count = 0;

    for (size_t i = 0; i < g_power.lock_count; ) {
        if (g_power.locks[i].entity_id == ent->id) {
            /* Remove lock by shifting */
            for (size_t j = i; j < g_power.lock_count - 1; j++) {
                g_power.locks[j] = g_power.locks[j + 1];
            }
            g_power.lock_count--;
            count++;
        } else {
            i++;
        }
    }

    return count;
}

bool ur_power_is_locked(ur_power_mode_t mode)
{
    for (size_t i = 0; i < g_power.lock_count; i++) {
        if (g_power.locks[i].mode == mode) {
            return true;
        }
    }
    return false;
}

/* ============================================================================
 * Sleep Control
 * ========================================================================== */

ur_power_mode_t ur_power_get_allowed_mode(void)
{
    /* Start from deepest sleep and work up */
    for (int mode = UR_POWER_DEEP_SLEEP; mode >= UR_POWER_IDLE; mode--) {
        if (!ur_power_is_locked((ur_power_mode_t)mode)) {
            return (ur_power_mode_t)mode;
        }
    }

    return UR_POWER_ACTIVE;
}

uint32_t ur_power_idle(uint32_t timeout_ms)
{
    if (!g_power.initialized) {
        ur_power_init(NULL);
    }

    if (g_power.hal == NULL) {
        return 0;
    }

    /* Determine allowed sleep mode */
    ur_power_mode_t allowed = ur_power_get_allowed_mode();

    /* If timeout not specified, use next event time */
    if (timeout_ms == 0) {
        timeout_ms = ur_power_get_next_event_ms();
    }

    /* Minimum idle threshold */
    if (timeout_ms < UR_CFG_POWER_IDLE_THRESHOLD_MS) {
        return 0;
    }

    uint32_t start = get_time();
    uint32_t actual = 0;

    switch (allowed) {
        case UR_POWER_IDLE:
            if (g_power.hal->enter_idle != NULL) {
                g_power.hal->enter_idle(timeout_ms);
            }
            break;

        case UR_POWER_LIGHT_SLEEP:
            if (g_power.hal->enter_light_sleep != NULL) {
                g_power.hal->enter_light_sleep(timeout_ms, UR_WAKE_ALL);
            }
            break;

        case UR_POWER_DEEP_SLEEP:
            if (g_power.hal->enter_deep_sleep != NULL) {
                g_power.hal->enter_deep_sleep(timeout_ms, UR_WAKE_ALL);
            }
            break;

        default:
            return 0;
    }

    actual = get_time() - start;
    track_time(allowed, actual);
    g_power.stats.wakeup_count++;

    if (g_power.hal->get_wakeup_reason != NULL) {
        g_power.stats.last_wakeup_reason = g_power.hal->get_wakeup_reason();
    }

    return actual;
}

uint32_t ur_power_enter_mode(ur_power_mode_t mode,
                              uint32_t timeout_ms,
                              uint8_t wake_sources)
{
    if (g_power.hal == NULL) {
        return 0;
    }

    uint32_t start = get_time();

    switch (mode) {
        case UR_POWER_IDLE:
            if (g_power.hal->enter_idle != NULL) {
                g_power.hal->enter_idle(timeout_ms);
            }
            break;

        case UR_POWER_LIGHT_SLEEP:
            if (g_power.hal->enter_light_sleep != NULL) {
                g_power.hal->enter_light_sleep(timeout_ms, wake_sources);
            }
            break;

        case UR_POWER_DEEP_SLEEP:
            if (g_power.hal->enter_deep_sleep != NULL) {
                g_power.hal->enter_deep_sleep(timeout_ms, wake_sources);
            }
            break;

        default:
            return 0;
    }

    uint32_t actual = get_time() - start;
    track_time(mode, actual);
    g_power.stats.wakeup_count++;

    return actual;
}

/* ============================================================================
 * Event Time Management
 * ========================================================================== */

void ur_power_set_next_event(ur_entity_t *ent, uint32_t time_ms)
{
    if (ent == NULL || ent->id == 0 || ent->id > UR_CFG_MAX_ENTITIES) {
        return;
    }

    g_power.next_events[ent->id - 1] = time_ms;
}

uint32_t ur_power_get_next_event_ms(void)
{
    uint32_t now = get_time();
    uint32_t min_delta = UINT32_MAX;

    for (size_t i = 0; i < UR_CFG_MAX_ENTITIES; i++) {
        uint32_t event_time = g_power.next_events[i];
        if (event_time != UINT32_MAX && event_time > now) {
            uint32_t delta = event_time - now;
            if (delta < min_delta) {
                min_delta = delta;
            }
        }
    }

    return min_delta;
}

/* ============================================================================
 * Statistics
 * ========================================================================== */

void ur_power_get_stats(ur_power_stats_t *stats)
{
    if (stats != NULL) {
        *stats = g_power.stats;
    }
}

void ur_power_reset_stats(void)
{
    memset(&g_power.stats, 0, sizeof(g_power.stats));
}

const char *ur_power_mode_name(ur_power_mode_t mode)
{
    static const char *names[] = {
        "ACTIVE",
        "IDLE",
        "LIGHT_SLEEP",
        "DEEP_SLEEP"
    };

    if (mode < UR_POWER_MODE_COUNT) {
        return names[mode];
    }
    return "UNKNOWN";
}

/* ============================================================================
 * Debug
 * ========================================================================== */

void ur_power_dump(void)
{
#if UR_CFG_ENABLE_LOGGING
    UR_LOGI("=== Power Management ===");
    UR_LOGI("Allowed mode: %s", ur_power_mode_name(ur_power_get_allowed_mode()));
    UR_LOGI("Active locks: %d", g_power.lock_count);

    for (size_t i = 0; i < g_power.lock_count; i++) {
        ur_entity_t *ent = ur_get_entity(g_power.locks[i].entity_id);
        UR_LOGI("  - Entity[%s] locks %s (count=%d)",
                ent ? ur_entity_name(ent) : "?",
                ur_power_mode_name(g_power.locks[i].mode),
                g_power.locks[i].count);
    }

    UR_LOGI("Stats: active=%lums, idle=%lums, light=%lums, deep=%lums, wakeups=%lu",
            g_power.stats.active_time_ms, g_power.stats.idle_time_ms,
            g_power.stats.light_sleep_ms, g_power.stats.deep_sleep_ms,
            g_power.stats.wakeup_count);
#endif
}

/* ============================================================================
 * No-op HAL
 * ========================================================================== */

static void noop_enter_idle(uint32_t timeout_ms)
{
#if UR_CFG_USE_FREERTOS
    vTaskDelay(pdMS_TO_TICKS(timeout_ms));
#else
    (void)timeout_ms;
#endif
}

static void noop_enter_light_sleep(uint32_t timeout_ms, uint8_t wake_sources)
{
    (void)wake_sources;
    noop_enter_idle(timeout_ms);
}

static void noop_enter_deep_sleep(uint32_t timeout_ms, uint8_t wake_sources)
{
    (void)wake_sources;
    noop_enter_idle(timeout_ms);
}

static ur_wake_source_t noop_get_wakeup_reason(void)
{
    return UR_WAKE_TIMER;
}

static uint32_t noop_get_time_ms(void)
{
    return ur_get_time_ms();
}

const ur_power_hal_t ur_power_hal_noop = {
    .enter_idle = noop_enter_idle,
    .enter_light_sleep = noop_enter_light_sleep,
    .enter_deep_sleep = noop_enter_deep_sleep,
    .get_wakeup_reason = noop_get_wakeup_reason,
    .get_time_ms = noop_get_time_ms,
};

/* ============================================================================
 * ESP-IDF HAL
 * ========================================================================== */

#if defined(CONFIG_IDF_TARGET) || defined(ESP_PLATFORM)
#include "esp_sleep.h"
#include "esp_timer.h"

static void esp_enter_idle(uint32_t timeout_ms)
{
    vTaskDelay(pdMS_TO_TICKS(timeout_ms));
}

static void esp_enter_light_sleep(uint32_t timeout_ms, uint8_t wake_sources)
{
    if (wake_sources & UR_WAKE_TIMER) {
        esp_sleep_enable_timer_wakeup(timeout_ms * 1000ULL);
    }

    esp_light_sleep_start();
}

static void esp_enter_deep_sleep(uint32_t timeout_ms, uint8_t wake_sources)
{
    if (wake_sources & UR_WAKE_TIMER) {
        esp_sleep_enable_timer_wakeup(timeout_ms * 1000ULL);
    }

    esp_deep_sleep_start();
    /* Never returns */
}

static ur_wake_source_t esp_get_wakeup_reason(void)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER:
            return UR_WAKE_TIMER;
        case ESP_SLEEP_WAKEUP_GPIO:
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
            return UR_WAKE_GPIO;
        case ESP_SLEEP_WAKEUP_UART:
            return UR_WAKE_UART;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            return UR_WAKE_TOUCH;
        default:
            return UR_WAKE_NONE;
    }
}

static uint32_t esp_get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

const ur_power_hal_t ur_power_hal_esp = {
    .enter_idle = esp_enter_idle,
    .enter_light_sleep = esp_enter_light_sleep,
    .enter_deep_sleep = esp_enter_deep_sleep,
    .get_wakeup_reason = esp_get_wakeup_reason,
    .get_time_ms = esp_get_time_ms,
};

#endif /* ESP_PLATFORM */

#endif /* UR_CFG_POWER_ENABLE */
