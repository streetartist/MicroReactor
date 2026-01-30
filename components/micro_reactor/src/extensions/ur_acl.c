/**
 * @file ur_acl.c
 * @brief MicroReactor Access Control List Implementation
 */

#include "ur_acl.h"
#include "ur_core.h"
#include "ur_utils.h"
#include <string.h>

#if UR_CFG_ACL_ENABLE

/* ============================================================================
 * Internal State
 * ========================================================================== */

typedef struct {
    uint16_t entity_id;
    ur_acl_rule_t rules[UR_CFG_ACL_MAX_RULES];
    size_t rule_count;
    ur_acl_default_t default_policy;
    ur_acl_transform_fn_t transform_fn;
    void *transform_ctx;
} acl_entry_t;

static struct {
    acl_entry_t entries[UR_CFG_MAX_ENTITIES];
    size_t entry_count;
    ur_acl_stats_t stats;
    bool initialized;
} g_acl = { 0 };

/* ============================================================================
 * Internal Functions
 * ========================================================================== */

static acl_entry_t *find_acl_entry(uint16_t entity_id)
{
    for (size_t i = 0; i < g_acl.entry_count; i++) {
        if (g_acl.entries[i].entity_id == entity_id) {
            return &g_acl.entries[i];
        }
    }
    return NULL;
}

static acl_entry_t *find_or_create_acl_entry(uint16_t entity_id)
{
    acl_entry_t *entry = find_acl_entry(entity_id);
    if (entry != NULL) {
        return entry;
    }

    if (g_acl.entry_count >= UR_CFG_MAX_ENTITIES) {
        return NULL;
    }

    entry = &g_acl.entries[g_acl.entry_count++];
    memset(entry, 0, sizeof(acl_entry_t));
    entry->entity_id = entity_id;
    entry->default_policy = UR_ACL_DEFAULT_ALLOW;

    return entry;
}

static bool match_source(uint16_t rule_src, uint16_t actual_src)
{
    if (rule_src == UR_ACL_SRC_ANY) {
        return true;
    }

    if (rule_src == UR_ACL_SRC_LOCAL) {
        /* Local entities have IDs 1-UR_CFG_MAX_ENTITIES */
        return actual_src >= 1 && actual_src <= UR_CFG_MAX_ENTITIES;
    }

    if (rule_src == UR_ACL_SRC_EXTERNAL) {
        /* External sources have IDs > UR_CFG_MAX_ENTITIES or 0 */
        return actual_src == 0 || actual_src > UR_CFG_MAX_ENTITIES;
    }

    return rule_src == actual_src;
}

static bool match_signal(uint16_t rule_sig, uint16_t actual_sig)
{
    if (rule_sig == UR_ACL_SIG_ANY) {
        return true;
    }

    if (rule_sig == UR_ACL_SIG_SYSTEM) {
        /* System signals: 0x0001-0x00FF */
        return actual_sig >= 0x0001 && actual_sig <= 0x00FF;
    }

    if (rule_sig == UR_ACL_SIG_USER) {
        /* User signals: 0x0100+ */
        return actual_sig >= 0x0100;
    }

    return rule_sig == actual_sig;
}

static const ur_acl_rule_t *find_matching_rule(const acl_entry_t *entry,
                                                 const ur_signal_t *sig)
{
    for (size_t i = 0; i < entry->rule_count; i++) {
        const ur_acl_rule_t *rule = &entry->rules[i];

        if (match_source(rule->src_id, sig->src_id) &&
            match_signal(rule->signal_id, sig->id)) {
            return rule;
        }
    }

    return NULL;
}

/* ============================================================================
 * Initialization
 * ========================================================================== */

ur_err_t ur_acl_init(void)
{
    memset(&g_acl, 0, sizeof(g_acl));
    g_acl.initialized = true;

    UR_LOGD("ACL: initialized");

    return UR_OK;
}

void ur_acl_reset(void)
{
    g_acl.entry_count = 0;
    memset(g_acl.entries, 0, sizeof(g_acl.entries));
    ur_acl_reset_stats();
}

/* ============================================================================
 * Rule Management
 * ========================================================================== */

ur_err_t ur_acl_register(ur_entity_t *ent,
                          const ur_acl_rule_t *rules,
                          size_t count)
{
    if (ent == NULL || (rules == NULL && count > 0)) {
        return UR_ERR_INVALID_ARG;
    }

    if (!g_acl.initialized) {
        ur_acl_init();
    }

    if (count > UR_CFG_ACL_MAX_RULES) {
        return UR_ERR_NO_MEMORY;
    }

    acl_entry_t *entry = find_or_create_acl_entry(ent->id);
    if (entry == NULL) {
        return UR_ERR_NO_MEMORY;
    }

    /* Copy rules */
    if (rules != NULL && count > 0) {
        memcpy(entry->rules, rules, count * sizeof(ur_acl_rule_t));
    }
    entry->rule_count = count;

    /* Sort by priority (insertion sort) */
    for (size_t i = 1; i < entry->rule_count; i++) {
        ur_acl_rule_t temp = entry->rules[i];
        size_t j = i;
        while (j > 0 && entry->rules[j - 1].priority > temp.priority) {
            entry->rules[j] = entry->rules[j - 1];
            j--;
        }
        entry->rules[j] = temp;
    }

    UR_LOGD("ACL: registered %d rules for Entity[%s]",
            count, ur_entity_name(ent));

    return UR_OK;
}

ur_err_t ur_acl_add_rule(ur_entity_t *ent, const ur_acl_rule_t *rule)
{
    if (ent == NULL || rule == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    if (!g_acl.initialized) {
        ur_acl_init();
    }

    acl_entry_t *entry = find_or_create_acl_entry(ent->id);
    if (entry == NULL) {
        return UR_ERR_NO_MEMORY;
    }

    if (entry->rule_count >= UR_CFG_ACL_MAX_RULES) {
        return UR_ERR_NO_MEMORY;
    }

    /* Insert in priority order */
    size_t insert_pos = entry->rule_count;
    for (size_t i = 0; i < entry->rule_count; i++) {
        if (rule->priority < entry->rules[i].priority) {
            insert_pos = i;
            break;
        }
    }

    /* Shift existing rules */
    for (size_t i = entry->rule_count; i > insert_pos; i--) {
        entry->rules[i] = entry->rules[i - 1];
    }

    entry->rules[insert_pos] = *rule;
    entry->rule_count++;

    return UR_OK;
}

int ur_acl_remove_rules(ur_entity_t *ent,
                         uint16_t src_id,
                         uint16_t signal_id)
{
    if (ent == NULL) {
        return 0;
    }

    acl_entry_t *entry = find_acl_entry(ent->id);
    if (entry == NULL) {
        return 0;
    }

    int removed = 0;

    for (size_t i = 0; i < entry->rule_count; ) {
        const ur_acl_rule_t *rule = &entry->rules[i];

        bool match = true;
        if (src_id != UR_ACL_SRC_ANY && rule->src_id != src_id) {
            match = false;
        }
        if (signal_id != UR_ACL_SIG_ANY && rule->signal_id != signal_id) {
            match = false;
        }

        if (match) {
            /* Remove by shifting */
            for (size_t j = i; j < entry->rule_count - 1; j++) {
                entry->rules[j] = entry->rules[j + 1];
            }
            entry->rule_count--;
            removed++;
        } else {
            i++;
        }
    }

    return removed;
}

void ur_acl_set_default(ur_entity_t *ent, ur_acl_default_t policy)
{
    if (ent == NULL) {
        return;
    }

    if (!g_acl.initialized) {
        ur_acl_init();
    }

    acl_entry_t *entry = find_or_create_acl_entry(ent->id);
    if (entry != NULL) {
        entry->default_policy = policy;
    }
}

void ur_acl_set_transform(ur_entity_t *ent,
                           ur_acl_transform_fn_t fn,
                           void *ctx)
{
    if (ent == NULL) {
        return;
    }

    acl_entry_t *entry = find_or_create_acl_entry(ent->id);
    if (entry != NULL) {
        entry->transform_fn = fn;
        entry->transform_ctx = ctx;
    }
}

/* ============================================================================
 * Checking
 * ========================================================================== */

ur_acl_action_t ur_acl_check(const ur_entity_t *ent, const ur_signal_t *sig)
{
    if (ent == NULL || sig == NULL) {
        return UR_ACL_ALLOW;
    }

    if (!g_acl.initialized) {
        return UR_ACL_ALLOW;
    }

    g_acl.stats.checked_count++;

    const acl_entry_t *entry = find_acl_entry(ent->id);
    if (entry == NULL) {
        /* No ACL configured - allow */
        g_acl.stats.default_count++;
        return UR_ACL_ALLOW;
    }

    const ur_acl_rule_t *rule = find_matching_rule(entry, sig);

    if (rule != NULL) {
        if (rule->flags & UR_ACL_FLAG_LOG) {
            UR_LOGD("ACL: rule match for Entity[%s], sig=0x%04X, src=%d, action=%d",
                    ur_entity_name(ent), sig->id, sig->src_id, rule->action);
        }
        return (ur_acl_action_t)rule->action;
    }

    /* No matching rule - use default policy */
    g_acl.stats.default_count++;
    return (entry->default_policy == UR_ACL_DEFAULT_ALLOW) ?
            UR_ACL_ALLOW : UR_ACL_DENY;
}

bool ur_acl_filter(const ur_entity_t *ent, ur_signal_t *sig)
{
    if (ent == NULL || sig == NULL) {
        return true;
    }

    ur_acl_action_t action = ur_acl_check(ent, sig);

    switch (action) {
        case UR_ACL_ALLOW:
            g_acl.stats.allowed_count++;
            return true;

        case UR_ACL_DENY:
            g_acl.stats.denied_count++;
            UR_LOGV("ACL: denied sig=0x%04X from src=%d to Entity[%s]",
                    sig->id, sig->src_id, ur_entity_name(ent));
            return false;

        case UR_ACL_LOG:
            g_acl.stats.logged_count++;
            UR_LOGI("ACL: [LOG] sig=0x%04X from src=%d to Entity[%s]",
                    sig->id, sig->src_id, ur_entity_name(ent));
            g_acl.stats.allowed_count++;
            return true;

        case UR_ACL_TRANSFORM: {
            const acl_entry_t *entry = find_acl_entry(ent->id);
            if (entry != NULL && entry->transform_fn != NULL) {
                bool allow = entry->transform_fn(sig, entry->transform_ctx);
                if (allow) {
                    g_acl.stats.transformed_count++;
                    g_acl.stats.allowed_count++;
                } else {
                    g_acl.stats.denied_count++;
                }
                return allow;
            }
            /* No transform function - allow */
            g_acl.stats.allowed_count++;
            return true;
        }

        default:
            g_acl.stats.allowed_count++;
            return true;
    }
}

/* ============================================================================
 * ACL Middleware
 * ========================================================================== */

ur_mw_result_t ur_acl_middleware(ur_entity_t *ent,
                                  ur_signal_t *sig,
                                  void *ctx)
{
    (void)ctx;

    if (ur_acl_filter(ent, sig)) {
        return UR_MW_CONTINUE;
    }

    return UR_MW_FILTERED;
}

ur_err_t ur_acl_enable_middleware(ur_entity_t *ent)
{
    return ur_register_middleware(ent, ur_acl_middleware, NULL, 0);
}

/* ============================================================================
 * Query
 * ========================================================================== */

size_t ur_acl_rule_count(const ur_entity_t *ent)
{
    if (ent == NULL) {
        return 0;
    }

    const acl_entry_t *entry = find_acl_entry(ent->id);
    return (entry != NULL) ? entry->rule_count : 0;
}

void ur_acl_get_stats(ur_acl_stats_t *stats)
{
    if (stats != NULL) {
        *stats = g_acl.stats;
    }
}

void ur_acl_reset_stats(void)
{
    memset(&g_acl.stats, 0, sizeof(g_acl.stats));
}

/* ============================================================================
 * Debug
 * ========================================================================== */

void ur_acl_dump(const ur_entity_t *ent)
{
#if UR_CFG_ENABLE_LOGGING
    static const char *action_names[] = { "DENY", "ALLOW", "LOG", "TRANSFORM" };

    if (ent != NULL) {
        const acl_entry_t *entry = find_acl_entry(ent->id);
        if (entry == NULL) {
            UR_LOGI("ACL: Entity[%s] has no ACL rules", ur_entity_name(ent));
            return;
        }

        UR_LOGI("=== ACL for Entity[%s] ===", ur_entity_name(ent));
        UR_LOGI("Default policy: %s",
                entry->default_policy == UR_ACL_DEFAULT_ALLOW ? "ALLOW" : "DENY");
        UR_LOGI("Rules: %d", entry->rule_count);

        for (size_t i = 0; i < entry->rule_count; i++) {
            const ur_acl_rule_t *rule = &entry->rules[i];

            char src_str[16];
            if (rule->src_id == UR_ACL_SRC_ANY) {
                strcpy(src_str, "*");
            } else if (rule->src_id == UR_ACL_SRC_LOCAL) {
                strcpy(src_str, "LOCAL");
            } else if (rule->src_id == UR_ACL_SRC_EXTERNAL) {
                strcpy(src_str, "EXTERNAL");
            } else {
                snprintf(src_str, sizeof(src_str), "%d", rule->src_id);
            }

            char sig_str[16];
            if (rule->signal_id == UR_ACL_SIG_ANY) {
                strcpy(sig_str, "*");
            } else if (rule->signal_id == UR_ACL_SIG_SYSTEM) {
                strcpy(sig_str, "SYSTEM");
            } else if (rule->signal_id == UR_ACL_SIG_USER) {
                strcpy(sig_str, "USER");
            } else {
                snprintf(sig_str, sizeof(sig_str), "0x%04X", rule->signal_id);
            }

            UR_LOGI("  [%d] src=%s sig=%s -> %s",
                    i, src_str, sig_str,
                    rule->action < 4 ? action_names[rule->action] : "?");
        }
    } else {
        /* Dump all */
        UR_LOGI("=== All ACL Rules ===");
        for (size_t i = 0; i < g_acl.entry_count; i++) {
            ur_entity_t *e = ur_get_entity(g_acl.entries[i].entity_id);
            if (e != NULL) {
                ur_acl_dump(e);
            }
        }
    }

    UR_LOGI("Stats: checked=%lu, allowed=%lu, denied=%lu, logged=%lu, default=%lu",
            g_acl.stats.checked_count, g_acl.stats.allowed_count,
            g_acl.stats.denied_count, g_acl.stats.logged_count,
            g_acl.stats.default_count);
#else
    (void)ent;
#endif
}

#endif /* UR_CFG_ACL_ENABLE */
