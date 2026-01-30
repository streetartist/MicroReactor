/**
 * @file ur_core.c
 * @brief MicroReactor core engine implementation
 *
 * Implements the dispatcher, FSM, cascading lookup, middleware chain,
 * and entity management.
 */

#include "ur_core.h"
#include "ur_utils.h"
#include "ur_trace.h"
#include <string.h>

#if UR_CFG_USE_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#endif

/* ============================================================================
 * Global Entity Registry
 * ========================================================================== */

static ur_entity_t *g_entity_registry[UR_CFG_MAX_ENTITIES] = { NULL };
static size_t g_entity_count = 0;

/* ============================================================================
 * Internal Function Prototypes
 * ========================================================================== */

static const ur_state_def_t *find_state(const ur_entity_t *ent, uint16_t state_id);
static const ur_rule_t *find_rule_in_state(const ur_state_def_t *state, uint16_t signal_id);
static const ur_rule_t *find_rule_in_mixins(const ur_entity_t *ent, uint16_t signal_id);
static const ur_rule_t *cascading_lookup(const ur_entity_t *ent, uint16_t signal_id);
static ur_err_t execute_transition(ur_entity_t *ent, uint16_t next_state, const ur_signal_t *sig);
static ur_mw_result_t run_middleware_chain(ur_entity_t *ent, ur_signal_t *sig);
static void sort_middleware(ur_entity_t *ent);

/* ============================================================================
 * Time Functions
 * ========================================================================== */

uint32_t ur_get_time_ms(void)
{
#if UR_CFG_USE_FREERTOS
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
#else
    return 0;
#endif
}

bool ur_in_isr(void)
{
#if UR_CFG_USE_FREERTOS
    return xPortInIsrContext();
#else
    return false;
#endif
}

/* ============================================================================
 * Entity Initialization
 * ========================================================================== */

ur_err_t ur_init(ur_entity_t *ent, const ur_entity_config_t *config)
{
    if (ent == NULL || config == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    if (config->state_count == 0 || config->states == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    /* Zero-initialize the entity */
    memset(ent, 0, sizeof(ur_entity_t));

    /* Copy configuration */
    ent->id = config->id;
    ent->name = config->name;
    ent->states = config->states;
    ent->state_count = config->state_count;
    ent->initial_state = config->initial_state;
    ent->current_state = 0; /* Not yet started */
    ent->user_data = config->user_data;
    ent->flags = UR_FLAG_NONE;

#if UR_CFG_USE_FREERTOS
    /* Create static inbox queue */
    ent->inbox = xQueueCreateStatic(
        UR_CFG_INBOX_SIZE,
        sizeof(ur_signal_t),
        ent->inbox_buffer,
        &ent->inbox_static
    );

    if (ent->inbox == NULL) {
        return UR_ERR_NO_MEMORY;
    }
#endif

    UR_LOGD("Entity[%s] initialized, id=%d", ur_entity_name(ent), ent->id);

    return UR_OK;
}

ur_err_t ur_start(ur_entity_t *ent)
{
    if (ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    if (ent->flags & UR_FLAG_ACTIVE) {
        return UR_ERR_INVALID_STATE;
    }

    /* Set active flag */
    ent->flags |= UR_FLAG_ACTIVE;

    /* Transition to initial state */
    ur_err_t err = ur_set_state(ent, ent->initial_state);
    if (err != UR_OK) {
        ent->flags &= ~UR_FLAG_ACTIVE;
        return err;
    }

    /* Send init signal to self */
    ur_signal_t init_sig = ur_signal_create(SIG_SYS_INIT, ent->id);
    ur_emit(ent, init_sig);

    UR_LOGI("Entity[%s] started in state %d", ur_entity_name(ent), ent->current_state);

    return UR_OK;
}

ur_err_t ur_stop(ur_entity_t *ent)
{
    if (ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    if (!(ent->flags & UR_FLAG_ACTIVE)) {
        return UR_ERR_INVALID_STATE;
    }

    /* Exit current state */
    const ur_state_def_t *state = find_state(ent, ent->current_state);
    if (state != NULL && state->on_exit != NULL) {
        ur_signal_t exit_sig = ur_signal_create(SIG_SYS_EXIT, ent->id);
        state->on_exit(ent, &exit_sig);
    }

    /* Clear state */
    ent->current_state = 0;
    ent->flags &= ~UR_FLAG_ACTIVE;

    /* Clear inbox */
    ur_inbox_clear(ent);

    /* Reset flow state */
    ent->flow_line = 0;
    ent->flow_wait_sig = SIG_NONE;
    ent->flow_wait_until = 0;
    ent->flags &= ~UR_FLAG_FLOW_RUNNING;

    UR_LOGI("Entity[%s] stopped", ur_entity_name(ent));

    return UR_OK;
}

ur_err_t ur_suspend(ur_entity_t *ent)
{
    if (ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    ent->flags |= UR_FLAG_SUSPENDED;

    UR_LOGD("Entity[%s] suspended", ur_entity_name(ent));

    return UR_OK;
}

ur_err_t ur_resume(ur_entity_t *ent)
{
    if (ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    ent->flags &= ~UR_FLAG_SUSPENDED;

    UR_LOGD("Entity[%s] resumed", ur_entity_name(ent));

    return UR_OK;
}

/* ============================================================================
 * Signal Emission
 * ========================================================================== */

ur_err_t ur_emit(ur_entity_t *target, ur_signal_t sig)
{
    if (target == NULL) {
        return UR_ERR_INVALID_ARG;
    }

#if UR_CFG_ENABLE_TIMESTAMPS
    if (sig.timestamp == 0) {
        sig.timestamp = ur_get_time_ms();
    }
#endif

#if UR_CFG_USE_FREERTOS
    if (ur_in_isr()) {
        BaseType_t woken = pdFALSE;
        ur_err_t err = ur_emit_from_isr(target, sig, &woken);
        portYIELD_FROM_ISR(woken);
        return err;
    }

    if (xQueueSend(target->inbox, &sig, 0) != pdTRUE) {
        UR_LOGW("Entity[%s] inbox full, signal 0x%04X dropped",
                ur_entity_name(target), sig.id);
        return UR_ERR_QUEUE_FULL;
    }
#else
    (void)sig;
    return UR_ERR_DISABLED;
#endif

    UR_LOGV("Signal 0x%04X -> Entity[%s]", sig.id, ur_entity_name(target));

    return UR_OK;
}

ur_err_t ur_emit_from_isr(ur_entity_t *target, ur_signal_t sig, BaseType_t *pxWoken)
{
    if (target == NULL) {
        return UR_ERR_INVALID_ARG;
    }

#if UR_CFG_ENABLE_TIMESTAMPS
    if (sig.timestamp == 0) {
        sig.timestamp = ur_get_time_ms();
    }
#endif

#if UR_CFG_USE_FREERTOS
    if (xQueueSendFromISR(target->inbox, &sig, pxWoken) != pdTRUE) {
        return UR_ERR_QUEUE_FULL;
    }
#else
    (void)sig;
    (void)pxWoken;
    return UR_ERR_DISABLED;
#endif

    return UR_OK;
}

ur_err_t ur_emit_to_id(uint16_t target_id, ur_signal_t sig)
{
    ur_entity_t *target = ur_get_entity(target_id);
    if (target == NULL) {
        return UR_ERR_NOT_FOUND;
    }

    return ur_emit(target, sig);
}

int ur_broadcast(ur_signal_t sig)
{
    int count = 0;

    for (size_t i = 0; i < UR_CFG_MAX_ENTITIES; i++) {
        if (g_entity_registry[i] != NULL) {
            if (ur_emit(g_entity_registry[i], sig) == UR_OK) {
                count++;
            }
        }
    }

    return count;
}

/* ============================================================================
 * Dispatch Loop
 * ========================================================================== */

ur_err_t ur_dispatch(ur_entity_t *ent, uint32_t timeout_ms)
{
    if (ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    /* Check if entity is active and not suspended */
    if (!(ent->flags & UR_FLAG_ACTIVE) || (ent->flags & UR_FLAG_SUSPENDED)) {
        return UR_ERR_INVALID_STATE;
    }

    ur_signal_t sig;

#if UR_CFG_USE_FREERTOS
    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);

    if (xQueueReceive(ent->inbox, &sig, ticks) != pdTRUE) {
        return UR_ERR_TIMEOUT;
    }
#else
    (void)timeout_ms;
    return UR_ERR_DISABLED;
#endif

    /* Trace: dispatch start */
    UR_TRACE_START(ent->id, sig.id);

    UR_LOG_SIGNAL(ent, &sig);

    /* Run middleware chain */
#if UR_CFG_MAX_MIDDLEWARE > 0
    ur_mw_result_t mw_result = run_middleware_chain(ent, &sig);
    if (mw_result == UR_MW_FILTERED) {
        UR_LOGV("Signal 0x%04X filtered by middleware", sig.id);
        UR_TRACE_END(ent->id, sig.id);
        return UR_OK;
    }
    if (mw_result == UR_MW_HANDLED) {
        UR_LOGV("Signal 0x%04X handled by middleware", sig.id);
        UR_TRACE_END(ent->id, sig.id);
        return UR_OK;
    }
#endif

    /* Cascading rule lookup */
    const ur_rule_t *rule = cascading_lookup(ent, sig.id);

    if (rule != NULL) {
        uint16_t next_state = rule->next_state;

        /* Execute action if present */
        if (rule->action != NULL) {
            uint16_t action_result = rule->action(ent, &sig);
            /* Action can override next state */
            if (action_result != 0) {
                next_state = action_result;
            }
        }

        /* Perform state transition if needed */
        if (next_state != 0 && next_state != ent->current_state) {
            uint16_t old_state = ent->current_state;
            execute_transition(ent, next_state, &sig);
            UR_TRACE_TRANSITION(ent->id, old_state, next_state);
        }
    } else {
        UR_LOGV("No rule found for signal 0x%04X in state %d",
                sig.id, ent->current_state);
    }

    /* Trace: dispatch end */
    UR_TRACE_END(ent->id, sig.id);

    return UR_OK;
}

int ur_dispatch_all(ur_entity_t *ent)
{
    int count = 0;

    while (ur_dispatch(ent, 0) == UR_OK) {
        count++;
    }

    return count;
}

int ur_dispatch_multi(ur_entity_t **entities, size_t count)
{
    int total = 0;

    for (size_t i = 0; i < count; i++) {
        if (entities[i] != NULL) {
            if (ur_dispatch(entities[i], 0) == UR_OK) {
                total++;
            }
        }
    }

    return total;
}

/* ============================================================================
 * State Management
 * ========================================================================== */

uint16_t ur_get_state(const ur_entity_t *ent)
{
    if (ent == NULL) {
        return 0;
    }
    return ent->current_state;
}

ur_err_t ur_set_state(ur_entity_t *ent, uint16_t state_id)
{
    if (ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    /* Find target state */
    const ur_state_def_t *new_state = find_state(ent, state_id);
    if (new_state == NULL) {
        UR_LOGW("Entity[%s]: Invalid state %d", ur_entity_name(ent), state_id);
        return UR_ERR_NOT_FOUND;
    }

    ur_signal_t trans_sig = ur_signal_create(SIG_NONE, ent->id);

    /* Exit current state if active */
    if (ent->current_state != 0) {
        const ur_state_def_t *old_state = find_state(ent, ent->current_state);
        if (old_state != NULL && old_state->on_exit != NULL) {
            trans_sig.id = SIG_SYS_EXIT;
            old_state->on_exit(ent, &trans_sig);
        }

        /* Reset flow state on state change */
        ent->flow_line = 0;
        ent->flow_wait_sig = SIG_NONE;
        ent->flow_wait_until = 0;
        ent->flags &= ~UR_FLAG_FLOW_RUNNING;
    }

    uint16_t old_state_id = ent->current_state;
    ent->current_state = state_id;

    /* Enter new state */
    if (new_state->on_entry != NULL) {
        trans_sig.id = SIG_SYS_ENTRY;
        new_state->on_entry(ent, &trans_sig);
    }

    UR_LOG_TRANSITION(ent, old_state_id, state_id);

    return UR_OK;
}

bool ur_in_state(const ur_entity_t *ent, uint16_t state_id)
{
    if (ent == NULL) {
        return false;
    }

    if (ent->current_state == state_id) {
        return true;
    }

#if UR_CFG_ENABLE_HSM
    /* Check parent chain */
    const ur_state_def_t *state = find_state(ent, ent->current_state);
    while (state != NULL && state->parent_id != 0) {
        if (state->parent_id == state_id) {
            return true;
        }
        state = find_state(ent, state->parent_id);
    }
#endif

    return false;
}

/* ============================================================================
 * Mixin Management
 * ========================================================================== */

ur_err_t ur_bind_mixin(ur_entity_t *ent, const ur_mixin_t *mixin)
{
#if UR_CFG_MAX_MIXINS_PER_ENTITY > 0
    if (ent == NULL || mixin == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    if (ent->mixin_count >= UR_CFG_MAX_MIXINS_PER_ENTITY) {
        return UR_ERR_NO_MEMORY;
    }

    /* Insert sorted by priority */
    size_t insert_pos = ent->mixin_count;
    for (size_t i = 0; i < ent->mixin_count; i++) {
        if (mixin->priority < ent->mixins[i]->priority) {
            insert_pos = i;
            break;
        }
    }

    /* Shift existing mixins */
    for (size_t i = ent->mixin_count; i > insert_pos; i--) {
        ent->mixins[i] = ent->mixins[i - 1];
    }

    ent->mixins[insert_pos] = mixin;
    ent->mixin_count++;

    UR_LOGD("Entity[%s]: Bound mixin '%s' at priority %d",
            ur_entity_name(ent), mixin->name ? mixin->name : "unnamed",
            mixin->priority);

    return UR_OK;
#else
    (void)ent;
    (void)mixin;
    return UR_ERR_DISABLED;
#endif
}

ur_err_t ur_unbind_mixin(ur_entity_t *ent, const ur_mixin_t *mixin)
{
#if UR_CFG_MAX_MIXINS_PER_ENTITY > 0
    if (ent == NULL || mixin == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < ent->mixin_count; i++) {
        if (ent->mixins[i] == mixin) {
            /* Shift remaining mixins */
            for (size_t j = i; j < ent->mixin_count - 1; j++) {
                ent->mixins[j] = ent->mixins[j + 1];
            }
            ent->mixin_count--;
            return UR_OK;
        }
    }

    return UR_ERR_NOT_FOUND;
#else
    (void)ent;
    (void)mixin;
    return UR_ERR_DISABLED;
#endif
}

/* ============================================================================
 * Middleware Management
 * ========================================================================== */

ur_err_t ur_register_middleware(ur_entity_t *ent,
                                 ur_middleware_fn_t fn,
                                 void *ctx,
                                 uint8_t priority)
{
#if UR_CFG_MAX_MIDDLEWARE > 0
    if (ent == NULL || fn == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    if (ent->middleware_count >= UR_CFG_MAX_MIDDLEWARE) {
        return UR_ERR_NO_MEMORY;
    }

    ent->middleware[ent->middleware_count].fn = fn;
    ent->middleware[ent->middleware_count].ctx = ctx;
    ent->middleware[ent->middleware_count].priority = priority;
    ent->middleware[ent->middleware_count].enabled = 1;
    ent->middleware_count++;

    /* Sort by priority */
    sort_middleware(ent);

    UR_LOGD("Entity[%s]: Registered middleware at priority %d",
            ur_entity_name(ent), priority);

    return UR_OK;
#else
    (void)ent;
    (void)fn;
    (void)ctx;
    (void)priority;
    return UR_ERR_DISABLED;
#endif
}

ur_err_t ur_unregister_middleware(ur_entity_t *ent, ur_middleware_fn_t fn)
{
#if UR_CFG_MAX_MIDDLEWARE > 0
    if (ent == NULL || fn == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < ent->middleware_count; i++) {
        if (ent->middleware[i].fn == fn) {
            /* Shift remaining */
            for (size_t j = i; j < ent->middleware_count - 1; j++) {
                ent->middleware[j] = ent->middleware[j + 1];
            }
            ent->middleware_count--;
            return UR_OK;
        }
    }

    return UR_ERR_NOT_FOUND;
#else
    (void)ent;
    (void)fn;
    return UR_ERR_DISABLED;
#endif
}

ur_err_t ur_set_middleware_enabled(ur_entity_t *ent,
                                    ur_middleware_fn_t fn,
                                    bool enabled)
{
#if UR_CFG_MAX_MIDDLEWARE > 0
    if (ent == NULL || fn == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < ent->middleware_count; i++) {
        if (ent->middleware[i].fn == fn) {
            ent->middleware[i].enabled = enabled ? 1 : 0;
            return UR_OK;
        }
    }

    return UR_ERR_NOT_FOUND;
#else
    (void)ent;
    (void)fn;
    (void)enabled;
    return UR_ERR_DISABLED;
#endif
}

/* ============================================================================
 * Entity Registry
 * ========================================================================== */

ur_err_t ur_register_entity(ur_entity_t *ent)
{
    if (ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    if (ent->id == 0 || ent->id > UR_CFG_MAX_ENTITIES) {
        return UR_ERR_INVALID_ARG;
    }

    uint16_t idx = ent->id - 1;

    if (g_entity_registry[idx] != NULL) {
        return UR_ERR_ALREADY_EXISTS;
    }

    g_entity_registry[idx] = ent;
    g_entity_count++;

    UR_LOGD("Entity[%s] registered with id=%d", ur_entity_name(ent), ent->id);

    return UR_OK;
}

ur_err_t ur_unregister_entity(ur_entity_t *ent)
{
    if (ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    if (ent->id == 0 || ent->id > UR_CFG_MAX_ENTITIES) {
        return UR_ERR_INVALID_ARG;
    }

    uint16_t idx = ent->id - 1;

    if (g_entity_registry[idx] != ent) {
        return UR_ERR_NOT_FOUND;
    }

    g_entity_registry[idx] = NULL;
    g_entity_count--;

    return UR_OK;
}

ur_entity_t *ur_get_entity(uint16_t id)
{
    if (id == 0 || id > UR_CFG_MAX_ENTITIES) {
        return NULL;
    }

    return g_entity_registry[id - 1];
}

size_t ur_get_entity_count(void)
{
    return g_entity_count;
}

/* ============================================================================
 * Inbox Utilities
 * ========================================================================== */

size_t ur_inbox_count(const ur_entity_t *ent)
{
    if (ent == NULL) {
        return 0;
    }

#if UR_CFG_USE_FREERTOS
    return uxQueueMessagesWaiting(ent->inbox);
#else
    return 0;
#endif
}

bool ur_inbox_empty(const ur_entity_t *ent)
{
    return ur_inbox_count(ent) == 0;
}

void ur_inbox_clear(ur_entity_t *ent)
{
    if (ent == NULL) {
        return;
    }

#if UR_CFG_USE_FREERTOS
    xQueueReset(ent->inbox);
#endif
}

/* ============================================================================
 * Internal Functions
 * ========================================================================== */

static const ur_state_def_t *find_state(const ur_entity_t *ent, uint16_t state_id)
{
    if (ent == NULL || ent->states == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < ent->state_count; i++) {
        if (ent->states[i].id == state_id) {
            return &ent->states[i];
        }
    }

    return NULL;
}

static const ur_rule_t *find_rule_in_state(const ur_state_def_t *state, uint16_t signal_id)
{
    if (state == NULL || state->rules == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < state->rule_count; i++) {
        if (state->rules[i].signal_id == signal_id) {
            return &state->rules[i];
        }
    }

    return NULL;
}

static const ur_rule_t *find_rule_in_mixins(const ur_entity_t *ent, uint16_t signal_id)
{
#if UR_CFG_MAX_MIXINS_PER_ENTITY > 0
    for (size_t i = 0; i < ent->mixin_count; i++) {
        const ur_mixin_t *mixin = ent->mixins[i];
        if (mixin != NULL && mixin->rules != NULL) {
            for (size_t j = 0; j < mixin->rule_count; j++) {
                if (mixin->rules[j].signal_id == signal_id) {
                    return &mixin->rules[j];
                }
            }
        }
    }
#else
    (void)ent;
    (void)signal_id;
#endif

    return NULL;
}

/**
 * Cascading rule lookup:
 * 1. Main rules (current state match)
 * 2. Mixin tables (state-agnostic)
 * 3. Parent state rules (HSM bubble-up)
 */
static const ur_rule_t *cascading_lookup(const ur_entity_t *ent, uint16_t signal_id)
{
    const ur_rule_t *rule = NULL;

    /* 1. Look in current state rules */
    const ur_state_def_t *state = find_state(ent, ent->current_state);
    if (state != NULL) {
        rule = find_rule_in_state(state, signal_id);
        if (rule != NULL) {
            return rule;
        }
    }

    /* 2. Look in mixins */
    rule = find_rule_in_mixins(ent, signal_id);
    if (rule != NULL) {
        return rule;
    }

#if UR_CFG_ENABLE_HSM
    /* 3. HSM bubble-up: look in parent states */
    while (state != NULL && state->parent_id != 0) {
        state = find_state(ent, state->parent_id);
        if (state != NULL) {
            rule = find_rule_in_state(state, signal_id);
            if (rule != NULL) {
                return rule;
            }
        }
    }
#endif

    return NULL;
}

static ur_err_t execute_transition(ur_entity_t *ent, uint16_t next_state, const ur_signal_t *sig)
{
    (void)sig;
    return ur_set_state(ent, next_state);
}

#if UR_CFG_MAX_MIDDLEWARE > 0
static ur_mw_result_t run_middleware_chain(ur_entity_t *ent, ur_signal_t *sig)
{
    for (size_t i = 0; i < ent->middleware_count; i++) {
        if (ent->middleware[i].enabled && ent->middleware[i].fn != NULL) {
            ur_mw_result_t result = ent->middleware[i].fn(
                ent, sig, ent->middleware[i].ctx);

            if (result == UR_MW_FILTERED || result == UR_MW_HANDLED) {
                return result;
            }
            /* UR_MW_CONTINUE and UR_MW_TRANSFORM continue to next middleware */
        }
    }

    return UR_MW_CONTINUE;
}

static void sort_middleware(ur_entity_t *ent)
{
    /* Simple insertion sort (small array) */
    for (size_t i = 1; i < ent->middleware_count; i++) {
        ur_middleware_t temp = ent->middleware[i];
        size_t j = i;
        while (j > 0 && ent->middleware[j - 1].priority > temp.priority) {
            ent->middleware[j] = ent->middleware[j - 1];
            j--;
        }
        ent->middleware[j] = temp;
    }
}
#endif
