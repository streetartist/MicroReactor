/**
 * @file ur_core.h
 * @brief MicroReactor core engine API
 *
 * Provides entity initialization, signal emission, and dispatch loop.
 */

#ifndef UR_CORE_H
#define UR_CORE_H

#include "ur_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Entity Management
 * ========================================================================== */

/**
 * @brief Initialize an entity
 *
 * Sets up the entity control block with the provided configuration.
 * Creates the inbox queue and sets the initial state.
 *
 * @param ent       Pointer to entity control block (must be static)
 * @param config    Entity configuration
 * @return UR_OK on success, error code otherwise
 */
ur_err_t ur_init(ur_entity_t *ent, const ur_entity_config_t *config);

/**
 * @brief Start an entity (enter initial state)
 *
 * Transitions to initial state and sends SIG_SYS_INIT.
 *
 * @param ent   Entity to start
 * @return UR_OK on success
 */
ur_err_t ur_start(ur_entity_t *ent);

/**
 * @brief Stop an entity
 *
 * Exits current state and marks entity as inactive.
 *
 * @param ent   Entity to stop
 * @return UR_OK on success
 */
ur_err_t ur_stop(ur_entity_t *ent);

/**
 * @brief Suspend an entity
 *
 * Pauses signal processing without exiting current state.
 *
 * @param ent   Entity to suspend
 * @return UR_OK on success
 */
ur_err_t ur_suspend(ur_entity_t *ent);

/**
 * @brief Resume a suspended entity
 *
 * @param ent   Entity to resume
 * @return UR_OK on success
 */
ur_err_t ur_resume(ur_entity_t *ent);

/* ============================================================================
 * Signal Emission
 * ========================================================================== */

/**
 * @brief Emit a signal to an entity
 *
 * Automatically detects ISR context and uses appropriate queue method.
 * Adds timestamp if enabled.
 *
 * @param target    Target entity
 * @param sig       Signal to emit
 * @return UR_OK on success, UR_ERR_QUEUE_FULL if inbox full
 */
ur_err_t ur_emit(ur_entity_t *target, ur_signal_t sig);

/**
 * @brief Emit a signal from ISR context
 *
 * Explicit ISR-safe version of ur_emit.
 *
 * @param target    Target entity
 * @param sig       Signal to emit
 * @param pxWoken   Set to pdTRUE if a higher priority task was woken
 * @return UR_OK on success
 */
ur_err_t ur_emit_from_isr(ur_entity_t *target, ur_signal_t sig,
                          BaseType_t *pxWoken);

/**
 * @brief Emit signal by entity ID (requires entity registry)
 *
 * @param target_id Target entity ID
 * @param sig       Signal to emit
 * @return UR_OK on success, UR_ERR_NOT_FOUND if entity not registered
 */
ur_err_t ur_emit_to_id(uint16_t target_id, ur_signal_t sig);

/**
 * @brief Broadcast signal to all registered entities
 *
 * @param sig   Signal to broadcast
 * @return Number of entities that received the signal
 */
int ur_broadcast(ur_signal_t sig);

/* ============================================================================
 * Dispatch Loop
 * ========================================================================== */

/**
 * @brief Process pending signals for an entity
 *
 * Dequeues one signal from inbox and processes through:
 * 1. Middleware chain
 * 2. Current state rules
 * 3. Mixin rules
 * 4. Parent state rules (HSM bubble-up)
 *
 * @param ent           Entity to process
 * @param timeout_ms    Timeout for waiting on inbox (0 = no wait)
 * @return UR_OK if signal processed, UR_ERR_TIMEOUT if no signal
 */
ur_err_t ur_dispatch(ur_entity_t *ent, uint32_t timeout_ms);

/**
 * @brief Process all pending signals for an entity
 *
 * Calls ur_dispatch repeatedly until inbox is empty.
 *
 * @param ent   Entity to process
 * @return Number of signals processed
 */
int ur_dispatch_all(ur_entity_t *ent);

/**
 * @brief Run dispatch loop for multiple entities
 *
 * Processes one signal from each entity in round-robin fashion.
 *
 * @param entities  Array of entity pointers
 * @param count     Number of entities
 * @return Total signals processed
 */
int ur_dispatch_multi(ur_entity_t **entities, size_t count);

/**
 * @brief Tickless dispatch loop iteration (recommended)
 *
 * Single-call dispatch loop that:
 * 1. Processes all pending signals from all entities
 * 2. Automatically sends SIG_SYS_TIMEOUT to flows waiting on UR_AWAIT_TIME
 * 3. Delays briefly if no signals were processed
 *
 * Usage:
 * @code
 * while (1) {
 *     ur_run(entities, count, 10);  // 10ms idle delay
 * }
 * @endcode
 *
 * @param entities  Array of entity pointers
 * @param count     Number of entities
 * @param idle_ms   Delay in ms when no signals processed (0 = no delay)
 * @return Total signals processed
 */
int ur_run(ur_entity_t **entities, size_t count, uint32_t idle_ms);

/* ============================================================================
 * State Management
 * ========================================================================== */

/**
 * @brief Get current state ID
 *
 * @param ent   Entity
 * @return Current state ID
 */
uint16_t ur_get_state(const ur_entity_t *ent);

/**
 * @brief Force state transition (bypasses rules)
 *
 * Executes exit/entry actions but no rule matching.
 * Use with caution - normally let rules handle transitions.
 *
 * @param ent       Entity
 * @param state_id  Target state ID
 * @return UR_OK on success
 */
ur_err_t ur_set_state(ur_entity_t *ent, uint16_t state_id);

/**
 * @brief Check if entity is in given state
 *
 * Also returns true for parent states in HSM mode.
 *
 * @param ent       Entity
 * @param state_id  State to check
 * @return true if in state (or child of state)
 */
bool ur_in_state(const ur_entity_t *ent, uint16_t state_id);

/* ============================================================================
 * Mixin Management
 * ========================================================================== */

/**
 * @brief Attach a mixin to an entity
 *
 * @param ent   Entity
 * @param mixin Mixin to attach
 * @return UR_OK on success, UR_ERR_NO_MEMORY if max mixins reached
 */
ur_err_t ur_bind_mixin(ur_entity_t *ent, const ur_mixin_t *mixin);

/**
 * @brief Detach a mixin from an entity
 *
 * @param ent   Entity
 * @param mixin Mixin to detach
 * @return UR_OK on success, UR_ERR_NOT_FOUND if not attached
 */
ur_err_t ur_unbind_mixin(ur_entity_t *ent, const ur_mixin_t *mixin);

/* ============================================================================
 * Middleware Management
 * ========================================================================== */

/**
 * @brief Register middleware in the processing chain
 *
 * @param ent       Entity
 * @param fn        Middleware function
 * @param ctx       Context pointer
 * @param priority  Execution priority (lower = first)
 * @return UR_OK on success
 */
ur_err_t ur_register_middleware(ur_entity_t *ent,
                                 ur_middleware_fn_t fn,
                                 void *ctx,
                                 uint8_t priority);

/**
 * @brief Unregister middleware
 *
 * @param ent   Entity
 * @param fn    Middleware function to remove
 * @return UR_OK on success
 */
ur_err_t ur_unregister_middleware(ur_entity_t *ent, ur_middleware_fn_t fn);

/**
 * @brief Enable/disable a middleware
 *
 * @param ent       Entity
 * @param fn        Middleware function
 * @param enabled   Enable state
 * @return UR_OK on success
 */
ur_err_t ur_set_middleware_enabled(ur_entity_t *ent,
                                    ur_middleware_fn_t fn,
                                    bool enabled);

/* ============================================================================
 * Entity Registry
 * ========================================================================== */

/**
 * @brief Register entity in global registry
 *
 * Required for ur_emit_to_id and ur_broadcast.
 *
 * @param ent   Entity to register
 * @return UR_OK on success
 */
ur_err_t ur_register_entity(ur_entity_t *ent);

/**
 * @brief Unregister entity from global registry
 *
 * @param ent   Entity to unregister
 * @return UR_OK on success
 */
ur_err_t ur_unregister_entity(ur_entity_t *ent);

/**
 * @brief Look up entity by ID
 *
 * @param id    Entity ID
 * @return Entity pointer or NULL if not found
 */
ur_entity_t *ur_get_entity(uint16_t id);

/**
 * @brief Get number of registered entities
 *
 * @return Count of registered entities
 */
size_t ur_get_entity_count(void);

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

/**
 * @brief Check if currently in ISR context
 *
 * @return true if in ISR
 */
bool ur_in_isr(void);

/**
 * @brief Get current timestamp in milliseconds
 *
 * @return Timestamp in ms
 */
uint32_t ur_get_time_ms(void);

/**
 * @brief Get inbox queue depth
 *
 * @param ent   Entity
 * @return Number of pending signals
 */
size_t ur_inbox_count(const ur_entity_t *ent);

/**
 * @brief Check if inbox is empty
 *
 * @param ent   Entity
 * @return true if inbox empty
 */
bool ur_inbox_empty(const ur_entity_t *ent);

/**
 * @brief Clear all signals from inbox
 *
 * @param ent   Entity
 */
void ur_inbox_clear(ur_entity_t *ent);

#ifdef __cplusplus
}
#endif

#endif /* UR_CORE_H */
