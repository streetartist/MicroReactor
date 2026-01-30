/**
 * @file ur_supervisor.c
 * @brief MicroReactor self-healing supervisor
 *
 * Implements parent-child relationship tracking and entity restart.
 */

#include "ur_core.h"
#include "ur_utils.h"

#if UR_CFG_SUPERVISOR_ENABLE

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

/* ============================================================================
 * Types
 * ========================================================================== */

/**
 * @brief Supervisor control block
 */
typedef struct {
    ur_entity_t *supervisor;                            /**< Supervisor entity */
    ur_entity_t *children[UR_CFG_SUPERVISOR_MAX_CHILDREN]; /**< Child entities */
    uint8_t child_count;                                /**< Number of children */
    uint8_t restart_counts[UR_CFG_SUPERVISOR_MAX_CHILDREN]; /**< Restart counters */
    uint8_t max_restarts;                               /**< Max restarts before giving up */
} ur_supervisor_t;

/* ============================================================================
 * Static Data
 * ========================================================================== */

#define MAX_SUPERVISORS 4
static ur_supervisor_t g_supervisors[MAX_SUPERVISORS];
static size_t g_supervisor_count = 0;

/* Timer for delayed restart */
static StaticTimer_t g_restart_timer_static;
static TimerHandle_t g_restart_timer = NULL;

/* Entity pending restart */
static ur_entity_t *g_pending_restart_entity = NULL;

/* ============================================================================
 * Internal Functions
 * ========================================================================== */

static ur_supervisor_t *find_supervisor(ur_entity_t *ent)
{
    for (size_t i = 0; i < g_supervisor_count; i++) {
        if (g_supervisors[i].supervisor == ent) {
            return &g_supervisors[i];
        }
    }
    return NULL;
}

static ur_supervisor_t *find_supervisor_of_child(ur_entity_t *child)
{
    for (size_t i = 0; i < g_supervisor_count; i++) {
        for (size_t j = 0; j < g_supervisors[i].child_count; j++) {
            if (g_supervisors[i].children[j] == child) {
                return &g_supervisors[i];
            }
        }
    }
    return NULL;
}

static int find_child_index(ur_supervisor_t *sup, ur_entity_t *child)
{
    for (size_t i = 0; i < sup->child_count; i++) {
        if (sup->children[i] == child) {
            return (int)i;
        }
    }
    return -1;
}

static void restart_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;

    if (g_pending_restart_entity != NULL) {
        ur_entity_t *ent = g_pending_restart_entity;
        g_pending_restart_entity = NULL;

        UR_LOGI("[SUPERVISOR] Restarting entity[%s]", ur_entity_name(ent));

        /* Reset and restart the entity */
        ur_stop(ent);
        ur_start(ent);

        /* Send revive signal */
        ur_signal_t revive = ur_signal_create(SIG_SYS_REVIVE, 0);
        ur_emit(ent, revive);
    }
}

/* ============================================================================
 * Public API
 * ========================================================================== */

/**
 * @brief Create a supervisor for an entity
 */
ur_err_t ur_supervisor_create(ur_entity_t *supervisor_ent, uint8_t max_restarts)
{
    if (supervisor_ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    if (g_supervisor_count >= MAX_SUPERVISORS) {
        return UR_ERR_NO_MEMORY;
    }

    /* Check if already a supervisor */
    if (find_supervisor(supervisor_ent) != NULL) {
        return UR_ERR_ALREADY_EXISTS;
    }

    /* Initialize supervisor structure */
    ur_supervisor_t *sup = &g_supervisors[g_supervisor_count];
    memset(sup, 0, sizeof(ur_supervisor_t));
    sup->supervisor = supervisor_ent;
    sup->max_restarts = max_restarts;

    /* Mark entity as supervisor */
    supervisor_ent->flags |= UR_FLAG_SUPERVISOR;

    g_supervisor_count++;

    /* Create restart timer if needed */
    if (g_restart_timer == NULL) {
        g_restart_timer = xTimerCreateStatic(
            "sup_restart",
            pdMS_TO_TICKS(UR_CFG_SUPERVISOR_RESTART_DELAY_MS),
            pdFALSE,  /* One-shot */
            NULL,
            restart_timer_callback,
            &g_restart_timer_static
        );
    }

    UR_LOGI("[SUPERVISOR] Created for entity[%s], max_restarts=%d",
            ur_entity_name(supervisor_ent), max_restarts);

    return UR_OK;
}

/**
 * @brief Add a child entity to a supervisor
 */
ur_err_t ur_supervisor_add_child(ur_entity_t *supervisor_ent, ur_entity_t *child_ent)
{
    if (supervisor_ent == NULL || child_ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    ur_supervisor_t *sup = find_supervisor(supervisor_ent);
    if (sup == NULL) {
        return UR_ERR_NOT_FOUND;
    }

    if (sup->child_count >= UR_CFG_SUPERVISOR_MAX_CHILDREN) {
        return UR_ERR_NO_MEMORY;
    }

    /* Check if already supervised */
    if (find_supervisor_of_child(child_ent) != NULL) {
        return UR_ERR_ALREADY_EXISTS;
    }

    /* Add child */
    sup->children[sup->child_count] = child_ent;
    sup->restart_counts[sup->child_count] = 0;
    sup->child_count++;

    /* Mark child as supervised */
    child_ent->flags |= UR_FLAG_SUPERVISED;
#if UR_CFG_SUPERVISOR_ENABLE
    child_ent->supervisor_id = supervisor_ent->id;
#endif

    UR_LOGD("[SUPERVISOR] Child entity[%s] added to supervisor[%s]",
            ur_entity_name(child_ent), ur_entity_name(supervisor_ent));

    return UR_OK;
}

/**
 * @brief Remove a child entity from supervision
 */
ur_err_t ur_supervisor_remove_child(ur_entity_t *supervisor_ent, ur_entity_t *child_ent)
{
    if (supervisor_ent == NULL || child_ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    ur_supervisor_t *sup = find_supervisor(supervisor_ent);
    if (sup == NULL) {
        return UR_ERR_NOT_FOUND;
    }

    int idx = find_child_index(sup, child_ent);
    if (idx < 0) {
        return UR_ERR_NOT_FOUND;
    }

    /* Clear supervised flag */
    child_ent->flags &= ~UR_FLAG_SUPERVISED;
#if UR_CFG_SUPERVISOR_ENABLE
    child_ent->supervisor_id = 0;
#endif

    /* Remove from array */
    for (size_t i = idx; i < sup->child_count - 1; i++) {
        sup->children[i] = sup->children[i + 1];
        sup->restart_counts[i] = sup->restart_counts[i + 1];
    }
    sup->child_count--;

    return UR_OK;
}

/**
 * @brief Soft reset an entity
 *
 * Resets entity state without full stop/start cycle.
 */
ur_err_t ur_reset_entity(ur_entity_t *ent)
{
    if (ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    UR_LOGI("[SUPERVISOR] Soft reset entity[%s]", ur_entity_name(ent));

    /* Clear inbox */
    ur_inbox_clear(ent);

    /* Reset flow state */
    ent->flow_line = 0;
    ent->flow_wait_sig = SIG_NONE;
    ent->flow_wait_until = 0;
    ent->flags &= ~UR_FLAG_FLOW_RUNNING;

    /* Clear scratchpad */
    memset(ent->scratch, 0, UR_CFG_SCRATCHPAD_SIZE);

    /* Transition to initial state */
    return ur_set_state(ent, ent->initial_state);
}

/**
 * @brief Report an entity as dying/failed
 *
 * Called when an entity encounters a fatal error.
 */
ur_err_t ur_report_dying(ur_entity_t *ent, uint32_t reason)
{
    if (ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    UR_LOGW("[SUPERVISOR] Entity[%s] reported dying, reason=%d",
            ur_entity_name(ent), reason);

    /* Find supervisor */
    ur_supervisor_t *sup = find_supervisor_of_child(ent);
    if (sup == NULL) {
        /* Not supervised - just log */
        UR_LOGW("[SUPERVISOR] Entity[%s] is not supervised", ur_entity_name(ent));
        return UR_OK;
    }

    /* Send dying signal to supervisor */
    ur_signal_t dying = ur_signal_create_u32(SIG_SYS_DYING, ent->id, reason);
    ur_emit(sup->supervisor, dying);

    /* Check restart count */
    int idx = find_child_index(sup, ent);
    if (idx >= 0) {
        sup->restart_counts[idx]++;

        if (sup->restart_counts[idx] > sup->max_restarts) {
            UR_LOGE("[SUPERVISOR] Entity[%s] exceeded max restarts (%d), giving up",
                    ur_entity_name(ent), sup->max_restarts);
            return UR_ERR_INVALID_STATE;
        }

        /* Schedule restart */
        g_pending_restart_entity = ent;
        xTimerStart(g_restart_timer, 0);

        UR_LOGI("[SUPERVISOR] Scheduling restart for entity[%s] (attempt %d/%d)",
                ur_entity_name(ent), sup->restart_counts[idx], sup->max_restarts);
    }

    return UR_OK;
}

/**
 * @brief Get restart count for an entity
 */
uint8_t ur_get_restart_count(ur_entity_t *ent)
{
    if (ent == NULL) {
        return 0;
    }

    ur_supervisor_t *sup = find_supervisor_of_child(ent);
    if (sup == NULL) {
        return 0;
    }

    int idx = find_child_index(sup, ent);
    if (idx < 0) {
        return 0;
    }

    return sup->restart_counts[idx];
}

/**
 * @brief Reset restart count for an entity
 */
ur_err_t ur_reset_restart_count(ur_entity_t *ent)
{
    if (ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    ur_supervisor_t *sup = find_supervisor_of_child(ent);
    if (sup == NULL) {
        return UR_ERR_NOT_FOUND;
    }

    int idx = find_child_index(sup, ent);
    if (idx < 0) {
        return UR_ERR_NOT_FOUND;
    }

    sup->restart_counts[idx] = 0;

    return UR_OK;
}

/**
 * @brief Supervisor middleware - handles SIG_SYS_DYING
 */
ur_mw_result_t ur_mw_supervisor(ur_entity_t *ent, ur_signal_t *sig, void *ctx)
{
    (void)ctx;

    /* Only supervisors handle dying signals */
    if (!(ent->flags & UR_FLAG_SUPERVISOR)) {
        return UR_MW_CONTINUE;
    }

    if (sig->id == SIG_SYS_DYING) {
        /* Already handled in ur_report_dying */
        UR_LOGD("[SUPERVISOR] Received SIG_SYS_DYING from entity %d", sig->src_id);
        return UR_MW_HANDLED;
    }

    return UR_MW_CONTINUE;
}

#endif /* UR_CFG_SUPERVISOR_ENABLE */
