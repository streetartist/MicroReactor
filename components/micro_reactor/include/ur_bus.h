/**
 * @file ur_bus.h
 * @brief MicroReactor Publish/Subscribe Bus
 *
 * Topic-based pub/sub system that replaces O(N) broadcast with O(subscribers)
 * delivery. Entities subscribe to specific signal IDs (topics) and only receive
 * signals they care about.
 *
 * Key Benefits:
 * - Performance: O(subscribers) instead of O(all_entities)
 * - Decoupling: Publishers don't need to know about subscribers
 * - Memory: Static subscription table, no dynamic allocation
 */

#ifndef UR_BUS_H
#define UR_BUS_H

#include "ur_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration (can be overridden via Kconfig)
 * ========================================================================== */

#ifndef UR_CFG_BUS_ENABLE
#ifdef CONFIG_UR_BUS_ENABLE
#define UR_CFG_BUS_ENABLE           CONFIG_UR_BUS_ENABLE
#else
#define UR_CFG_BUS_ENABLE           1
#endif
#endif

#ifndef UR_CFG_BUS_MAX_TOPICS
#ifdef CONFIG_UR_BUS_MAX_TOPICS
#define UR_CFG_BUS_MAX_TOPICS       CONFIG_UR_BUS_MAX_TOPICS
#else
#define UR_CFG_BUS_MAX_TOPICS       64
#endif
#endif

#ifndef UR_CFG_BUS_MAX_SUBSCRIBERS
#ifdef CONFIG_UR_BUS_MAX_SUBSCRIBERS
#define UR_CFG_BUS_MAX_SUBSCRIBERS  CONFIG_UR_BUS_MAX_SUBSCRIBERS
#else
#define UR_CFG_BUS_MAX_SUBSCRIBERS  8
#endif
#endif

#if UR_CFG_BUS_ENABLE

/* ============================================================================
 * Types
 * ========================================================================== */

/**
 * @brief Subscription entry
 */
typedef struct {
    uint16_t topic_id;                              /**< Signal ID (topic) */
    uint16_t subscriber_count;                      /**< Number of subscribers */
    uint16_t subscribers[UR_CFG_BUS_MAX_SUBSCRIBERS]; /**< Subscriber entity IDs */
} ur_bus_topic_t;

/**
 * @brief Bus statistics
 */
typedef struct {
    uint32_t publish_count;         /**< Total publishes */
    uint32_t delivery_count;        /**< Total deliveries */
    uint32_t drop_count;            /**< Dropped (queue full) */
    uint32_t no_subscriber_count;   /**< Published with no subscribers */
} ur_bus_stats_t;

/* ============================================================================
 * Initialization
 * ========================================================================== */

/**
 * @brief Initialize the pub/sub bus
 *
 * Must be called before any subscribe/publish operations.
 *
 * @return UR_OK on success
 */
ur_err_t ur_bus_init(void);

/**
 * @brief Reset bus state (clear all subscriptions)
 */
void ur_bus_reset(void);

/* ============================================================================
 * Subscription Management
 * ========================================================================== */

/**
 * @brief Subscribe an entity to a topic (signal ID)
 *
 * After subscribing, the entity will receive signals published to this topic.
 *
 * @param ent       Entity to subscribe
 * @param topic_id  Signal ID to subscribe to
 * @return UR_OK on success, UR_ERR_NO_MEMORY if topic/subscriber limit reached
 *
 * @code
 * // UI entity subscribes to battery and wifi status signals
 * ur_subscribe(&ui_entity, SIG_BATTERY_LEVEL);
 * ur_subscribe(&ui_entity, SIG_WIFI_STATUS);
 * @endcode
 */
ur_err_t ur_subscribe(ur_entity_t *ent, uint16_t topic_id);

/**
 * @brief Subscribe entity by ID
 *
 * @param entity_id Entity ID (must be registered)
 * @param topic_id  Signal ID to subscribe to
 * @return UR_OK on success
 */
ur_err_t ur_subscribe_id(uint16_t entity_id, uint16_t topic_id);

/**
 * @brief Unsubscribe an entity from a topic
 *
 * @param ent       Entity to unsubscribe
 * @param topic_id  Signal ID to unsubscribe from
 * @return UR_OK on success, UR_ERR_NOT_FOUND if not subscribed
 */
ur_err_t ur_unsubscribe(ur_entity_t *ent, uint16_t topic_id);

/**
 * @brief Unsubscribe entity from all topics
 *
 * @param ent   Entity to unsubscribe
 * @return Number of topics unsubscribed from
 */
int ur_unsubscribe_all(ur_entity_t *ent);

/**
 * @brief Check if entity is subscribed to a topic
 *
 * @param ent       Entity to check
 * @param topic_id  Topic to check
 * @return true if subscribed
 */
bool ur_is_subscribed(const ur_entity_t *ent, uint16_t topic_id);

/* ============================================================================
 * Publishing
 * ========================================================================== */

/**
 * @brief Publish a signal to all subscribers of a topic
 *
 * Only entities that have subscribed to the signal's ID will receive it.
 * This is O(subscribers) instead of O(all_entities).
 *
 * @param sig   Signal to publish (sig.id is the topic)
 * @return Number of entities that received the signal
 *
 * @code
 * // Battery entity publishes level - doesn't need to know who listens
 * ur_signal_t sig = ur_signal_create(SIG_BATTERY_LEVEL, battery_ent.id);
 * sig.payload.u8[0] = battery_percent;
 * ur_publish(sig);
 * @endcode
 */
int ur_publish(ur_signal_t sig);

/**
 * @brief Publish signal from ISR context
 *
 * @param sig       Signal to publish
 * @param pxWoken   Set to pdTRUE if a task was woken
 * @return Number of entities that received the signal
 */
int ur_publish_from_isr(ur_signal_t sig, BaseType_t *pxWoken);

/**
 * @brief Publish with custom source ID
 *
 * Convenience function to create and publish in one call.
 *
 * @param topic_id  Signal ID (topic)
 * @param src_id    Source entity ID
 * @param payload   Payload value (u32)
 * @return Number of entities that received the signal
 */
int ur_publish_u32(uint16_t topic_id, uint16_t src_id, uint32_t payload);

/**
 * @brief Publish with pointer payload
 *
 * @param topic_id  Signal ID (topic)
 * @param src_id    Source entity ID
 * @param ptr       Pointer payload (must be static/stack)
 * @return Number of entities that received the signal
 */
int ur_publish_ptr(uint16_t topic_id, uint16_t src_id, void *ptr);

/* ============================================================================
 * Query Functions
 * ========================================================================== */

/**
 * @brief Get number of subscribers for a topic
 *
 * @param topic_id  Signal ID to query
 * @return Number of subscribers, 0 if topic not registered
 */
size_t ur_bus_subscriber_count(uint16_t topic_id);

/**
 * @brief Get total number of active topics
 *
 * @return Number of topics with at least one subscriber
 */
size_t ur_bus_topic_count(void);

/**
 * @brief Get bus statistics
 *
 * @param stats Output statistics structure
 */
void ur_bus_get_stats(ur_bus_stats_t *stats);

/**
 * @brief Reset bus statistics
 */
void ur_bus_reset_stats(void);

/* ============================================================================
 * Debug/Shell Support
 * ========================================================================== */

/**
 * @brief Print bus subscription table (for debugging)
 *
 * Outputs human-readable subscription information.
 */
void ur_bus_dump(void);

#endif /* UR_CFG_BUS_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* UR_BUS_H */
