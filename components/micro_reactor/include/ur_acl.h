/**
 * @file ur_acl.h
 * @brief MicroReactor Signal Firewall (Access Control)
 *
 * Source-based access control for signal routing:
 * - Protect critical entities from unauthorized signals
 * - Filter RPC/external signals at the boundary
 * - Implement zero-trust architecture for IoT devices
 *
 * ACL rules are evaluated in order; first match wins.
 * Default policy can be ALLOW or DENY.
 */

#ifndef UR_ACL_H
#define UR_ACL_H

#include "ur_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration
 * ========================================================================== */

#ifndef UR_CFG_ACL_ENABLE
#ifdef CONFIG_UR_ACL_ENABLE
#define UR_CFG_ACL_ENABLE           CONFIG_UR_ACL_ENABLE
#else
#define UR_CFG_ACL_ENABLE           1
#endif
#endif

#ifndef UR_CFG_ACL_MAX_RULES
#ifdef CONFIG_UR_ACL_MAX_RULES
#define UR_CFG_ACL_MAX_RULES        CONFIG_UR_ACL_MAX_RULES
#else
#define UR_CFG_ACL_MAX_RULES        32
#endif
#endif

#if UR_CFG_ACL_ENABLE

/* ============================================================================
 * Types
 * ========================================================================== */

/**
 * @brief ACL action
 */
typedef enum {
    UR_ACL_DENY = 0,        /**< Block signal */
    UR_ACL_ALLOW,           /**< Allow signal */
    UR_ACL_LOG,             /**< Allow but log */
    UR_ACL_TRANSFORM,       /**< Allow with transformation */
} ur_acl_action_t;

/**
 * @brief Special source IDs
 */
#define UR_ACL_SRC_ANY          0x0000  /**< Match any source */
#define UR_ACL_SRC_LOCAL        0xFFFE  /**< Match local entities only */
#define UR_ACL_SRC_EXTERNAL     0xFFFF  /**< Match external sources (RPC/wormhole) */

/**
 * @brief Special signal IDs
 */
#define UR_ACL_SIG_ANY          0x0000  /**< Match any signal */
#define UR_ACL_SIG_SYSTEM       0x00FF  /**< Match system signals (0x0001-0x00FF) */
#define UR_ACL_SIG_USER         0xFFFF  /**< Match user signals (0x0100+) */

/**
 * @brief ACL rule definition
 */
typedef struct {
    uint16_t src_id;            /**< Source entity ID (or UR_ACL_SRC_*) */
    uint16_t signal_id;         /**< Signal ID (or UR_ACL_SIG_*) */
    uint8_t action;             /**< ur_acl_action_t */
    uint8_t priority;           /**< Rule priority (lower = evaluated first) */
    uint8_t flags;              /**< Additional flags */
    uint8_t _reserved;          /**< Padding */
} ur_acl_rule_t;

/**
 * @brief ACL rule flags
 */
typedef enum {
    UR_ACL_FLAG_NONE = 0x00,
    UR_ACL_FLAG_LOG = 0x01,         /**< Log when rule matches */
    UR_ACL_FLAG_COUNT = 0x02,       /**< Count matches */
    UR_ACL_FLAG_ONESHOT = 0x04,     /**< Remove after first match */
} ur_acl_flags_t;

/**
 * @brief Default policy
 */
typedef enum {
    UR_ACL_DEFAULT_ALLOW = 0,   /**< Allow if no rule matches */
    UR_ACL_DEFAULT_DENY,        /**< Deny if no rule matches */
} ur_acl_default_t;

/**
 * @brief Transform callback
 *
 * Called when rule action is UR_ACL_TRANSFORM.
 *
 * @param sig   Signal (can be modified)
 * @param ctx   User context
 * @return true to allow (potentially modified) signal, false to deny
 */
typedef bool (*ur_acl_transform_fn_t)(ur_signal_t *sig, void *ctx);

/**
 * @brief ACL statistics
 */
typedef struct {
    uint32_t checked_count;     /**< Total signals checked */
    uint32_t allowed_count;     /**< Signals allowed */
    uint32_t denied_count;      /**< Signals denied */
    uint32_t logged_count;      /**< Signals logged */
    uint32_t transformed_count; /**< Signals transformed */
    uint32_t default_count;     /**< Signals using default policy */
} ur_acl_stats_t;

/* ============================================================================
 * Initialization
 * ========================================================================== */

/**
 * @brief Initialize ACL system
 *
 * @return UR_OK on success
 */
ur_err_t ur_acl_init(void);

/**
 * @brief Reset ACL (clear all rules)
 */
void ur_acl_reset(void);

/* ============================================================================
 * Rule Management
 * ========================================================================== */

/**
 * @brief Register ACL rules for an entity
 *
 * Replaces any existing rules for this entity.
 *
 * @param ent       Target entity
 * @param rules     Array of rules
 * @param count     Number of rules
 * @return UR_OK on success
 *
 * @code
 * static const ur_acl_rule_t brain_acl[] = {
 *     // Allow keypad signals
 *     { ID_KEYPAD, UR_ACL_SIG_ANY, UR_ACL_ALLOW, 0 },
 *     // Block external factory reset
 *     { UR_ACL_SRC_EXTERNAL, SIG_FACTORY_RESET, UR_ACL_DENY, 0 },
 *     // Allow external play/pause
 *     { UR_ACL_SRC_EXTERNAL, SIG_CMD_PLAY, UR_ACL_ALLOW, 0 },
 *     { UR_ACL_SRC_EXTERNAL, SIG_CMD_PAUSE, UR_ACL_ALLOW, 0 },
 * };
 * ur_acl_register(&brain_ent, brain_acl, 4);
 * @endcode
 */
ur_err_t ur_acl_register(ur_entity_t *ent,
                          const ur_acl_rule_t *rules,
                          size_t count);

/**
 * @brief Add a single rule
 *
 * @param ent       Target entity
 * @param rule      Rule to add
 * @return UR_OK on success
 */
ur_err_t ur_acl_add_rule(ur_entity_t *ent, const ur_acl_rule_t *rule);

/**
 * @brief Remove rules matching source and signal
 *
 * @param ent       Target entity
 * @param src_id    Source ID (UR_ACL_SRC_ANY = all sources)
 * @param signal_id Signal ID (UR_ACL_SIG_ANY = all signals)
 * @return Number of rules removed
 */
int ur_acl_remove_rules(ur_entity_t *ent,
                         uint16_t src_id,
                         uint16_t signal_id);

/**
 * @brief Set default policy for an entity
 *
 * @param ent       Target entity
 * @param policy    Default policy
 */
void ur_acl_set_default(ur_entity_t *ent, ur_acl_default_t policy);

/**
 * @brief Set transform callback for an entity
 *
 * @param ent       Target entity
 * @param fn        Transform function
 * @param ctx       User context
 */
void ur_acl_set_transform(ur_entity_t *ent,
                           ur_acl_transform_fn_t fn,
                           void *ctx);

/* ============================================================================
 * Checking
 * ========================================================================== */

/**
 * @brief Check if signal should be allowed
 *
 * Evaluates ACL rules and returns the action.
 * Does not modify the signal.
 *
 * @param ent   Target entity
 * @param sig   Signal to check
 * @return ACL action
 */
ur_acl_action_t ur_acl_check(const ur_entity_t *ent, const ur_signal_t *sig);

/**
 * @brief Check and potentially transform signal
 *
 * @param ent   Target entity
 * @param sig   Signal (may be modified if action is TRANSFORM)
 * @return true if signal should be delivered
 */
bool ur_acl_filter(const ur_entity_t *ent, ur_signal_t *sig);

/* ============================================================================
 * ACL Middleware
 * ========================================================================== */

/**
 * @brief ACL middleware function
 *
 * Can be registered as entity middleware for automatic filtering.
 *
 * @param ent   Entity
 * @param sig   Signal
 * @param ctx   Context (unused)
 * @return Middleware result
 */
ur_mw_result_t ur_acl_middleware(ur_entity_t *ent,
                                  ur_signal_t *sig,
                                  void *ctx);

/**
 * @brief Enable ACL middleware for an entity
 *
 * Convenience function to register ACL as highest-priority middleware.
 *
 * @param ent   Entity
 * @return UR_OK on success
 */
ur_err_t ur_acl_enable_middleware(ur_entity_t *ent);

/* ============================================================================
 * Query
 * ========================================================================== */

/**
 * @brief Get number of rules for an entity
 *
 * @param ent   Entity
 * @return Number of rules
 */
size_t ur_acl_rule_count(const ur_entity_t *ent);

/**
 * @brief Get ACL statistics
 *
 * @param stats Output statistics
 */
void ur_acl_get_stats(ur_acl_stats_t *stats);

/**
 * @brief Reset ACL statistics
 */
void ur_acl_reset_stats(void);

/* ============================================================================
 * Debug
 * ========================================================================== */

/**
 * @brief Print ACL rules for an entity
 *
 * @param ent   Entity (NULL = print all)
 */
void ur_acl_dump(const ur_entity_t *ent);

/* ============================================================================
 * Convenience Macros
 * ========================================================================== */

/**
 * @brief Define an ACL rule
 */
#define UR_ACL_RULE(src, sig, act) \
    { .src_id = (src), .signal_id = (sig), .action = (act), .priority = 0, .flags = 0 }

/**
 * @brief Allow all signals from a source
 */
#define UR_ACL_ALLOW_FROM(src) \
    UR_ACL_RULE(src, UR_ACL_SIG_ANY, UR_ACL_ALLOW)

/**
 * @brief Deny all signals from a source
 */
#define UR_ACL_DENY_FROM(src) \
    UR_ACL_RULE(src, UR_ACL_SIG_ANY, UR_ACL_DENY)

/**
 * @brief Allow specific signal from any source
 */
#define UR_ACL_ALLOW_SIG(sig) \
    UR_ACL_RULE(UR_ACL_SRC_ANY, sig, UR_ACL_ALLOW)

/**
 * @brief Deny specific signal from any source
 */
#define UR_ACL_DENY_SIG(sig) \
    UR_ACL_RULE(UR_ACL_SRC_ANY, sig, UR_ACL_DENY)

/**
 * @brief Block all external signals
 */
#define UR_ACL_DENY_EXTERNAL() \
    UR_ACL_RULE(UR_ACL_SRC_EXTERNAL, UR_ACL_SIG_ANY, UR_ACL_DENY)

#endif /* UR_CFG_ACL_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* UR_ACL_H */
