/**
 * @file ur_panic.c
 * @brief MicroReactor panic handler and black box logging
 *
 * Provides panic hooks and signal history for debugging.
 */

#include "ur_core.h"
#include "ur_utils.h"

#if UR_CFG_PANIC_ENABLE

#include <string.h>

#if defined(CONFIG_IDF_TARGET) || defined(ESP_PLATFORM)
#include "esp_timer.h"
#endif

/* ============================================================================
 * Static Data
 * ========================================================================== */

static ur_blackbox_entry_t g_blackbox[UR_CFG_PANIC_BLACKBOX_SIZE];
static size_t g_blackbox_head = 0;
static size_t g_blackbox_count = 0;

static ur_panic_hook_t g_panic_hook = NULL;

/* ============================================================================
 * Black Box Functions
 * ========================================================================== */

/**
 * @brief Record a signal in the black box
 */
void ur_blackbox_record(const ur_entity_t *ent, const ur_signal_t *sig)
{
    if (ent == NULL || sig == NULL) {
        return;
    }

    ur_blackbox_entry_t *entry = &g_blackbox[g_blackbox_head];

    entry->entity_id = ent->id;
    entry->signal_id = sig->id;
    entry->src_id = sig->src_id;
    entry->state = ent->current_state;
    entry->timestamp = sig->timestamp;

    g_blackbox_head = (g_blackbox_head + 1) % UR_CFG_PANIC_BLACKBOX_SIZE;

    if (g_blackbox_count < UR_CFG_PANIC_BLACKBOX_SIZE) {
        g_blackbox_count++;
    }
}

/**
 * @brief Get black box history
 *
 * @param history   Output array (must be at least UR_CFG_PANIC_BLACKBOX_SIZE)
 * @param max_count Maximum entries to retrieve
 * @return Number of entries copied
 */
size_t ur_blackbox_get_history(ur_blackbox_entry_t *history, size_t max_count)
{
    if (history == NULL || max_count == 0) {
        return 0;
    }

    size_t count = (max_count < g_blackbox_count) ? max_count : g_blackbox_count;
    size_t start_idx;

    if (g_blackbox_count < UR_CFG_PANIC_BLACKBOX_SIZE) {
        /* Buffer not yet wrapped */
        start_idx = 0;
    } else {
        /* Start from oldest entry */
        start_idx = g_blackbox_head;
    }

    for (size_t i = 0; i < count; i++) {
        size_t idx = (start_idx + i) % UR_CFG_PANIC_BLACKBOX_SIZE;
        history[i] = g_blackbox[idx];
    }

    return count;
}

/**
 * @brief Clear the black box
 */
void ur_blackbox_clear(void)
{
    g_blackbox_head = 0;
    g_blackbox_count = 0;
    memset(g_blackbox, 0, sizeof(g_blackbox));
}

/**
 * @brief Get number of entries in black box
 */
size_t ur_blackbox_count(void)
{
    return g_blackbox_count;
}

/* ============================================================================
 * Panic Functions
 * ========================================================================== */

/**
 * @brief Register a panic hook
 *
 * The hook will be called when ur_panic() is invoked.
 */
void ur_panic_set_hook(ur_panic_hook_t hook)
{
    g_panic_hook = hook;
}

/**
 * @brief Trigger a panic
 *
 * @param reason    Panic reason string
 */
void ur_panic(const char *reason)
{
    UR_LOGE("[PANIC] %s", reason ? reason : "Unknown");

    /* Dump black box to log */
    UR_LOGE("[PANIC] Black box (%d entries):", g_blackbox_count);

    ur_blackbox_entry_t history[UR_CFG_PANIC_BLACKBOX_SIZE];
    size_t count = ur_blackbox_get_history(history, UR_CFG_PANIC_BLACKBOX_SIZE);

    for (size_t i = 0; i < count; i++) {
        UR_LOGE("  [%d] ent=%d state=%d sig=0x%04X src=%d ts=%u",
                i,
                history[i].entity_id,
                history[i].state,
                history[i].signal_id,
                history[i].src_id,
                history[i].timestamp);
    }

    /* Call panic hook if registered */
    if (g_panic_hook != NULL) {
        g_panic_hook(reason, history, count);
    }

    /* Halt */
    abort();
}

/**
 * @brief Panic with formatted message
 */
void ur_panic_with_info(const char *reason, const ur_entity_t *ent, const ur_signal_t *sig)
{
    if (ent != NULL && sig != NULL) {
        UR_LOGE("[PANIC] %s - Entity[%s] State=%d Signal=0x%04X",
                reason ? reason : "Unknown",
                ur_entity_name(ent),
                ent->current_state,
                sig->id);
    } else if (ent != NULL) {
        UR_LOGE("[PANIC] %s - Entity[%s] State=%d",
                reason ? reason : "Unknown",
                ur_entity_name(ent),
                ent->current_state);
    } else {
        UR_LOGE("[PANIC] %s", reason ? reason : "Unknown");
    }

    ur_panic(reason);
}

/* ============================================================================
 * Black Box Middleware
 * ========================================================================== */

/**
 * @brief Black box recording middleware
 *
 * Records all signals to the black box for post-mortem analysis.
 */
ur_mw_result_t ur_mw_blackbox(ur_entity_t *ent, ur_signal_t *sig, void *ctx)
{
    (void)ctx;

    ur_blackbox_record(ent, sig);

    return UR_MW_CONTINUE;
}

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

/**
 * @brief Print black box contents to log
 */
void ur_blackbox_dump(void)
{
    UR_LOGI("=== Black Box Dump (%d entries) ===", g_blackbox_count);

    ur_blackbox_entry_t history[UR_CFG_PANIC_BLACKBOX_SIZE];
    size_t count = ur_blackbox_get_history(history, UR_CFG_PANIC_BLACKBOX_SIZE);

    for (size_t i = 0; i < count; i++) {
        UR_LOGI("[%3d] T=%8u | Ent=%3d State=%3d | Sig=0x%04X Src=%3d",
                i,
                history[i].timestamp,
                history[i].entity_id,
                history[i].state,
                history[i].signal_id,
                history[i].src_id);
    }

    UR_LOGI("=== End Black Box ===");
}

/**
 * @brief Get the last recorded signal for an entity
 */
bool ur_blackbox_last_signal(uint16_t entity_id, ur_blackbox_entry_t *out)
{
    if (out == NULL || g_blackbox_count == 0) {
        return false;
    }

    /* Search backwards from head */
    for (size_t i = 0; i < g_blackbox_count; i++) {
        size_t idx = (g_blackbox_head + UR_CFG_PANIC_BLACKBOX_SIZE - 1 - i) % UR_CFG_PANIC_BLACKBOX_SIZE;

        if (g_blackbox[idx].entity_id == entity_id) {
            *out = g_blackbox[idx];
            return true;
        }
    }

    return false;
}

#endif /* UR_CFG_PANIC_ENABLE */

/* ============================================================================
 * Utility Function Implementations (always available)
 * ========================================================================== */

uint8_t ur_crc8(const uint8_t *data, size_t len);

/**
 * @brief Get current time in milliseconds
 */
uint32_t ur_time_ms(void)
{
#if UR_CFG_USE_FREERTOS
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
#else
    return 0;
#endif
}

/**
 * @brief Get current time in microseconds
 */
uint64_t ur_time_us(void)
{
#if UR_CFG_USE_FREERTOS
    return esp_timer_get_time();
#else
    return 0;
#endif
}

/**
 * @brief Check if time has elapsed
 */
bool ur_time_elapsed(uint32_t start, uint32_t duration)
{
    uint32_t now = ur_time_ms();
    return (now - start) >= duration;
}

/**
 * @brief Calculate time difference (handles wrap)
 */
uint32_t ur_time_diff(uint32_t start, uint32_t end)
{
    return end - start;
}

/**
 * @brief Create signal with timestamp
 */
ur_signal_t ur_signal_create(uint16_t id, uint16_t src_id)
{
    ur_signal_t sig = { 0 };
    sig.id = id;
    sig.src_id = src_id;
#if UR_CFG_ENABLE_TIMESTAMPS
    sig.timestamp = ur_time_ms();
#endif
    return sig;
}

/**
 * @brief Create signal with u32 payload
 */
ur_signal_t ur_signal_create_u32(uint16_t id, uint16_t src_id, uint32_t payload)
{
    ur_signal_t sig = ur_signal_create(id, src_id);
    sig.payload.u32[0] = payload;
    return sig;
}

/**
 * @brief Create signal with pointer payload
 */
ur_signal_t ur_signal_create_ptr(uint16_t id, uint16_t src_id, void *ptr)
{
    ur_signal_t sig = ur_signal_create(id, src_id);
    sig.ptr = ptr;
    return sig;
}

/**
 * @brief Copy a signal
 */
void ur_signal_copy(ur_signal_t *dst, const ur_signal_t *src)
{
    if (dst != NULL && src != NULL) {
        memcpy(dst, src, sizeof(ur_signal_t));
    }
}

/**
 * @brief Get entity name
 */
const char *ur_entity_name(const ur_entity_t *ent)
{
    if (ent == NULL || ent->name == NULL) {
        return "unnamed";
    }
    return ent->name;
}

/**
 * @brief Get state name (weak, user can override)
 */
__attribute__((weak))
const char *ur_state_name(const ur_entity_t *ent, uint16_t state_id)
{
    (void)ent;
    (void)state_id;
    return "unknown";
}

/**
 * @brief Get signal name (weak, user can override)
 */
__attribute__((weak))
const char *ur_signal_name(uint16_t signal_id)
{
    (void)signal_id;
    return "unknown";
}

/**
 * @brief Zero-fill memory
 */
void ur_memzero(void *ptr, size_t size)
{
    if (ptr != NULL && size > 0) {
        memset(ptr, 0, size);
    }
}

/**
 * @brief Copy memory
 */
void ur_memcpy(void *dst, const void *src, size_t size)
{
    if (dst != NULL && src != NULL && size > 0) {
        memcpy(dst, src, size);
    }
}
