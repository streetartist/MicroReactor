/**
 * @file ur_bus.c
 * @brief MicroReactor Publish/Subscribe Bus Implementation
 */

#include "ur_bus.h"
#include "ur_core.h"
#include "ur_utils.h"
#include <string.h>

#if UR_CFG_BUS_ENABLE

/* ============================================================================
 * Internal State
 * ========================================================================== */

static struct {
    ur_bus_topic_t topics[UR_CFG_BUS_MAX_TOPICS];
    size_t topic_count;
    ur_bus_stats_t stats;
    bool initialized;
} g_bus = { 0 };

/* ============================================================================
 * Internal Functions
 * ========================================================================== */

/**
 * @brief Find topic by ID
 */
static ur_bus_topic_t *find_topic(uint16_t topic_id)
{
    for (size_t i = 0; i < g_bus.topic_count; i++) {
        if (g_bus.topics[i].topic_id == topic_id) {
            return &g_bus.topics[i];
        }
    }
    return NULL;
}

/**
 * @brief Find or create topic
 */
static ur_bus_topic_t *find_or_create_topic(uint16_t topic_id)
{
    ur_bus_topic_t *topic = find_topic(topic_id);
    if (topic != NULL) {
        return topic;
    }

    /* Create new topic */
    if (g_bus.topic_count >= UR_CFG_BUS_MAX_TOPICS) {
        return NULL;
    }

    topic = &g_bus.topics[g_bus.topic_count++];
    memset(topic, 0, sizeof(ur_bus_topic_t));
    topic->topic_id = topic_id;

    return topic;
}

/**
 * @brief Check if entity is subscribed to topic
 */
static bool is_subscribed_to_topic(const ur_bus_topic_t *topic, uint16_t entity_id)
{
    for (size_t i = 0; i < topic->subscriber_count; i++) {
        if (topic->subscribers[i] == entity_id) {
            return true;
        }
    }
    return false;
}

/* ============================================================================
 * Initialization
 * ========================================================================== */

ur_err_t ur_bus_init(void)
{
    memset(&g_bus, 0, sizeof(g_bus));
    g_bus.initialized = true;

    UR_LOGD("Bus initialized (max_topics=%d, max_subs=%d)",
            UR_CFG_BUS_MAX_TOPICS, UR_CFG_BUS_MAX_SUBSCRIBERS);

    return UR_OK;
}

void ur_bus_reset(void)
{
    g_bus.topic_count = 0;
    memset(g_bus.topics, 0, sizeof(g_bus.topics));
    ur_bus_reset_stats();

    UR_LOGD("Bus reset");
}

/* ============================================================================
 * Subscription Management
 * ========================================================================== */

ur_err_t ur_subscribe(ur_entity_t *ent, uint16_t topic_id)
{
    if (ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    if (!g_bus.initialized) {
        ur_bus_init();
    }

    ur_bus_topic_t *topic = find_or_create_topic(topic_id);
    if (topic == NULL) {
        UR_LOGW("Bus: max topics reached");
        return UR_ERR_NO_MEMORY;
    }

    /* Check if already subscribed */
    if (is_subscribed_to_topic(topic, ent->id)) {
        return UR_OK;
    }

    /* Add subscriber */
    if (topic->subscriber_count >= UR_CFG_BUS_MAX_SUBSCRIBERS) {
        UR_LOGW("Bus: max subscribers for topic 0x%04X", topic_id);
        return UR_ERR_NO_MEMORY;
    }

    topic->subscribers[topic->subscriber_count++] = ent->id;

    UR_LOGD("Bus: Entity[%s] subscribed to 0x%04X",
            ur_entity_name(ent), topic_id);

    return UR_OK;
}

ur_err_t ur_subscribe_id(uint16_t entity_id, uint16_t topic_id)
{
    ur_entity_t *ent = ur_get_entity(entity_id);
    if (ent == NULL) {
        return UR_ERR_NOT_FOUND;
    }
    return ur_subscribe(ent, topic_id);
}

ur_err_t ur_unsubscribe(ur_entity_t *ent, uint16_t topic_id)
{
    if (ent == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    ur_bus_topic_t *topic = find_topic(topic_id);
    if (topic == NULL) {
        return UR_ERR_NOT_FOUND;
    }

    /* Find and remove subscriber */
    for (size_t i = 0; i < topic->subscriber_count; i++) {
        if (topic->subscribers[i] == ent->id) {
            /* Shift remaining subscribers */
            for (size_t j = i; j < topic->subscriber_count - 1; j++) {
                topic->subscribers[j] = topic->subscribers[j + 1];
            }
            topic->subscriber_count--;

            UR_LOGD("Bus: Entity[%s] unsubscribed from 0x%04X",
                    ur_entity_name(ent), topic_id);

            return UR_OK;
        }
    }

    return UR_ERR_NOT_FOUND;
}

int ur_unsubscribe_all(ur_entity_t *ent)
{
    if (ent == NULL) {
        return 0;
    }

    int count = 0;

    for (size_t t = 0; t < g_bus.topic_count; t++) {
        ur_bus_topic_t *topic = &g_bus.topics[t];

        for (size_t i = 0; i < topic->subscriber_count; i++) {
            if (topic->subscribers[i] == ent->id) {
                /* Shift remaining subscribers */
                for (size_t j = i; j < topic->subscriber_count - 1; j++) {
                    topic->subscribers[j] = topic->subscribers[j + 1];
                }
                topic->subscriber_count--;
                count++;
                break;
            }
        }
    }

    return count;
}

bool ur_is_subscribed(const ur_entity_t *ent, uint16_t topic_id)
{
    if (ent == NULL) {
        return false;
    }

    const ur_bus_topic_t *topic = find_topic(topic_id);
    if (topic == NULL) {
        return false;
    }

    return is_subscribed_to_topic(topic, ent->id);
}

/* ============================================================================
 * Publishing
 * ========================================================================== */

int ur_publish(ur_signal_t sig)
{
    if (!g_bus.initialized) {
        ur_bus_init();
    }

    g_bus.stats.publish_count++;

    ur_bus_topic_t *topic = find_topic(sig.id);
    if (topic == NULL || topic->subscriber_count == 0) {
        g_bus.stats.no_subscriber_count++;
        UR_LOGV("Bus: No subscribers for 0x%04X", sig.id);
        return 0;
    }

    int delivered = 0;

    for (size_t i = 0; i < topic->subscriber_count; i++) {
        ur_entity_t *ent = ur_get_entity(topic->subscribers[i]);
        if (ent != NULL) {
            if (ur_emit(ent, sig) == UR_OK) {
                delivered++;
                g_bus.stats.delivery_count++;
            } else {
                g_bus.stats.drop_count++;
            }
        }
    }

    UR_LOGV("Bus: Published 0x%04X to %d subscribers", sig.id, delivered);

    return delivered;
}

int ur_publish_from_isr(ur_signal_t sig, BaseType_t *pxWoken)
{
    ur_bus_topic_t *topic = find_topic(sig.id);
    if (topic == NULL || topic->subscriber_count == 0) {
        return 0;
    }

    int delivered = 0;
    BaseType_t any_woken = pdFALSE;

    for (size_t i = 0; i < topic->subscriber_count; i++) {
        ur_entity_t *ent = ur_get_entity(topic->subscribers[i]);
        if (ent != NULL) {
            BaseType_t woken = pdFALSE;
            if (ur_emit_from_isr(ent, sig, &woken) == UR_OK) {
                delivered++;
                if (woken) {
                    any_woken = pdTRUE;
                }
            }
        }
    }

    if (pxWoken != NULL) {
        *pxWoken = any_woken;
    }

    return delivered;
}

int ur_publish_u32(uint16_t topic_id, uint16_t src_id, uint32_t payload)
{
    ur_signal_t sig = UR_SIGNAL_U32(topic_id, src_id, payload);
    return ur_publish(sig);
}

int ur_publish_ptr(uint16_t topic_id, uint16_t src_id, void *ptr)
{
    ur_signal_t sig = UR_SIGNAL_PTR(topic_id, src_id, ptr);
    return ur_publish(sig);
}

/* ============================================================================
 * Query Functions
 * ========================================================================== */

size_t ur_bus_subscriber_count(uint16_t topic_id)
{
    const ur_bus_topic_t *topic = find_topic(topic_id);
    return (topic != NULL) ? topic->subscriber_count : 0;
}

size_t ur_bus_topic_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < g_bus.topic_count; i++) {
        if (g_bus.topics[i].subscriber_count > 0) {
            count++;
        }
    }
    return count;
}

void ur_bus_get_stats(ur_bus_stats_t *stats)
{
    if (stats != NULL) {
        *stats = g_bus.stats;
    }
}

void ur_bus_reset_stats(void)
{
    memset(&g_bus.stats, 0, sizeof(g_bus.stats));
}

/* ============================================================================
 * Debug
 * ========================================================================== */

void ur_bus_dump(void)
{
#if UR_CFG_ENABLE_LOGGING
    UR_LOGI("=== Bus Subscription Table ===");
    UR_LOGI("Topics: %d/%d", g_bus.topic_count, UR_CFG_BUS_MAX_TOPICS);

    for (size_t i = 0; i < g_bus.topic_count; i++) {
        const ur_bus_topic_t *topic = &g_bus.topics[i];
        if (topic->subscriber_count > 0) {
            UR_LOGI("Topic 0x%04X: %d subscribers",
                    topic->topic_id, topic->subscriber_count);

            for (size_t j = 0; j < topic->subscriber_count; j++) {
                ur_entity_t *ent = ur_get_entity(topic->subscribers[j]);
                if (ent != NULL) {
                    UR_LOGI("  - Entity[%s] (id=%d)",
                            ur_entity_name(ent), topic->subscribers[j]);
                } else {
                    UR_LOGI("  - Entity %d (not found)", topic->subscribers[j]);
                }
            }
        }
    }

    UR_LOGI("Stats: pub=%lu, deliver=%lu, drop=%lu, no_sub=%lu",
            g_bus.stats.publish_count, g_bus.stats.delivery_count,
            g_bus.stats.drop_count, g_bus.stats.no_subscriber_count);
#endif
}

#endif /* UR_CFG_BUS_ENABLE */
