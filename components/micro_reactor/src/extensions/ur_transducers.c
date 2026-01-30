/**
 * @file ur_transducers.c
 * @brief MicroReactor standard middleware (transducers)
 *
 * Provides common middleware functions for signal processing:
 * - Logger
 * - Debounce
 * - Throttle
 * - Filter
 */

#include "ur_core.h"
#include "ur_utils.h"

#if UR_CFG_MAX_MIDDLEWARE > 0

#include <string.h>

/* ============================================================================
 * Logger Middleware
 * ========================================================================== */

/**
 * @brief Logger middleware - logs all signals
 *
 * Context: ur_mw_logger_ctx_t* or NULL for defaults
 */
typedef struct {
    uint16_t filter_signal;     /**< Only log this signal (0 = all) */
    bool log_payload;           /**< Log payload data */
} ur_mw_logger_ctx_t;

ur_mw_result_t ur_mw_logger(ur_entity_t *ent, ur_signal_t *sig, void *ctx)
{
    ur_mw_logger_ctx_t *log_ctx = (ur_mw_logger_ctx_t *)ctx;

    /* Filter if configured */
    if (log_ctx != NULL && log_ctx->filter_signal != 0) {
        if (sig->id != log_ctx->filter_signal) {
            return UR_MW_CONTINUE;
        }
    }

    /* Log the signal */
    if (log_ctx != NULL && log_ctx->log_payload) {
        UR_LOGI("[LOG] Entity[%s] State=%d <- Sig=0x%04X Src=%d Payload=0x%08X",
                ur_entity_name(ent),
                ent->current_state,
                sig->id,
                sig->src_id,
                sig->payload.u32[0]);
    } else {
        UR_LOGI("[LOG] Entity[%s] State=%d <- Sig=0x%04X Src=%d",
                ur_entity_name(ent),
                ent->current_state,
                sig->id,
                sig->src_id);
    }

    return UR_MW_CONTINUE;
}

/* ============================================================================
 * Debounce Middleware
 * ========================================================================== */

/**
 * @brief Debounce context - must be static per entity
 */
typedef struct {
    uint16_t signal_id;         /**< Signal to debounce */
    uint32_t debounce_ms;       /**< Debounce period in ms */
    uint32_t last_time;         /**< Last accepted signal time */
} ur_mw_debounce_ctx_t;

ur_mw_result_t ur_mw_debounce(ur_entity_t *ent, ur_signal_t *sig, void *ctx)
{
    (void)ent;

    if (ctx == NULL) {
        return UR_MW_CONTINUE;
    }

    ur_mw_debounce_ctx_t *db_ctx = (ur_mw_debounce_ctx_t *)ctx;

    /* Only debounce configured signal */
    if (sig->id != db_ctx->signal_id) {
        return UR_MW_CONTINUE;
    }

    uint32_t now = ur_get_time_ms();
    uint32_t elapsed = now - db_ctx->last_time;

    if (elapsed < db_ctx->debounce_ms) {
        /* Filter out - too soon */
        UR_LOGV("[DEBOUNCE] Signal 0x%04X filtered (elapsed=%dms < %dms)",
                sig->id, elapsed, db_ctx->debounce_ms);
        return UR_MW_FILTERED;
    }

    /* Accept signal */
    db_ctx->last_time = now;

    return UR_MW_CONTINUE;
}

/* ============================================================================
 * Throttle Middleware
 * ========================================================================== */

/**
 * @brief Throttle context - must be static per entity
 */
typedef struct {
    uint16_t signal_id;         /**< Signal to throttle */
    uint32_t interval_ms;       /**< Minimum interval between signals */
    uint32_t last_time;         /**< Last passed signal time */
    uint32_t count_dropped;     /**< Number of dropped signals */
} ur_mw_throttle_ctx_t;

ur_mw_result_t ur_mw_throttle(ur_entity_t *ent, ur_signal_t *sig, void *ctx)
{
    (void)ent;

    if (ctx == NULL) {
        return UR_MW_CONTINUE;
    }

    ur_mw_throttle_ctx_t *th_ctx = (ur_mw_throttle_ctx_t *)ctx;

    /* Only throttle configured signal */
    if (sig->id != th_ctx->signal_id) {
        return UR_MW_CONTINUE;
    }

    uint32_t now = ur_get_time_ms();
    uint32_t elapsed = now - th_ctx->last_time;

    if (elapsed < th_ctx->interval_ms) {
        /* Filter out - rate limited */
        th_ctx->count_dropped++;
        return UR_MW_FILTERED;
    }

    /* Pass signal */
    th_ctx->last_time = now;

    /* Optionally add dropped count to payload */
    if (th_ctx->count_dropped > 0) {
        UR_LOGV("[THROTTLE] Passing signal 0x%04X (dropped %d)",
                sig->id, th_ctx->count_dropped);
        th_ctx->count_dropped = 0;
    }

    return UR_MW_CONTINUE;
}

/* ============================================================================
 * Filter Middleware
 * ========================================================================== */

/**
 * @brief Filter predicate function type
 */
typedef bool (*ur_filter_predicate_t)(const ur_entity_t *ent,
                                       const ur_signal_t *sig,
                                       void *user_data);

/**
 * @brief Filter context
 */
typedef struct {
    ur_filter_predicate_t predicate;    /**< Filter function */
    void *user_data;                     /**< User data for predicate */
    bool invert;                         /**< Invert predicate result */
} ur_mw_filter_ctx_t;

ur_mw_result_t ur_mw_filter(ur_entity_t *ent, ur_signal_t *sig, void *ctx)
{
    if (ctx == NULL) {
        return UR_MW_CONTINUE;
    }

    ur_mw_filter_ctx_t *f_ctx = (ur_mw_filter_ctx_t *)ctx;

    if (f_ctx->predicate == NULL) {
        return UR_MW_CONTINUE;
    }

    bool pass = f_ctx->predicate(ent, sig, f_ctx->user_data);

    if (f_ctx->invert) {
        pass = !pass;
    }

    return pass ? UR_MW_CONTINUE : UR_MW_FILTERED;
}

/* ============================================================================
 * Signal ID Filter Middleware
 * ========================================================================== */

/**
 * @brief Signal ID whitelist/blacklist context
 */
typedef struct {
    const uint16_t *signal_list;    /**< Array of signal IDs */
    size_t count;                   /**< Number of IDs in list */
    bool is_whitelist;              /**< true = whitelist, false = blacklist */
} ur_mw_sigfilter_ctx_t;

ur_mw_result_t ur_mw_sigfilter(ur_entity_t *ent, ur_signal_t *sig, void *ctx)
{
    (void)ent;

    if (ctx == NULL) {
        return UR_MW_CONTINUE;
    }

    ur_mw_sigfilter_ctx_t *sf_ctx = (ur_mw_sigfilter_ctx_t *)ctx;

    if (sf_ctx->signal_list == NULL || sf_ctx->count == 0) {
        return UR_MW_CONTINUE;
    }

    /* Search for signal in list */
    bool found = false;
    for (size_t i = 0; i < sf_ctx->count; i++) {
        if (sf_ctx->signal_list[i] == sig->id) {
            found = true;
            break;
        }
    }

    if (sf_ctx->is_whitelist) {
        /* Whitelist: pass only if found */
        return found ? UR_MW_CONTINUE : UR_MW_FILTERED;
    } else {
        /* Blacklist: filter if found */
        return found ? UR_MW_FILTERED : UR_MW_CONTINUE;
    }
}

/* ============================================================================
 * Transform Middleware
 * ========================================================================== */

/**
 * @brief Transform function type
 */
typedef void (*ur_transform_fn_t)(ur_entity_t *ent,
                                   ur_signal_t *sig,
                                   void *user_data);

/**
 * @brief Transform context
 */
typedef struct {
    ur_transform_fn_t transform;    /**< Transform function */
    void *user_data;                /**< User data */
} ur_mw_transform_ctx_t;

ur_mw_result_t ur_mw_transform(ur_entity_t *ent, ur_signal_t *sig, void *ctx)
{
    if (ctx == NULL) {
        return UR_MW_CONTINUE;
    }

    ur_mw_transform_ctx_t *t_ctx = (ur_mw_transform_ctx_t *)ctx;

    if (t_ctx->transform != NULL) {
        t_ctx->transform(ent, sig, t_ctx->user_data);
        return UR_MW_TRANSFORM;
    }

    return UR_MW_CONTINUE;
}

/* ============================================================================
 * State Guard Middleware
 * ========================================================================== */

/**
 * @brief State guard context - filter signals in certain states
 */
typedef struct {
    uint16_t signal_id;         /**< Signal to guard */
    const uint16_t *states;     /**< States where signal is allowed */
    size_t state_count;         /**< Number of states */
} ur_mw_stateguard_ctx_t;

ur_mw_result_t ur_mw_stateguard(ur_entity_t *ent, ur_signal_t *sig, void *ctx)
{
    if (ctx == NULL) {
        return UR_MW_CONTINUE;
    }

    ur_mw_stateguard_ctx_t *sg_ctx = (ur_mw_stateguard_ctx_t *)ctx;

    /* Only guard configured signal */
    if (sig->id != sg_ctx->signal_id) {
        return UR_MW_CONTINUE;
    }

    /* Check if current state is in allowed list */
    for (size_t i = 0; i < sg_ctx->state_count; i++) {
        if (sg_ctx->states[i] == ent->current_state) {
            return UR_MW_CONTINUE;
        }
    }

    /* State not in allowed list - filter */
    UR_LOGV("[STATEGUARD] Signal 0x%04X filtered in state %d",
            sig->id, ent->current_state);

    return UR_MW_FILTERED;
}

#endif /* UR_CFG_MAX_MIDDLEWARE > 0 */
