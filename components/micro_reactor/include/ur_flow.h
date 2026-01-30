/**
 * @file ur_flow.h
 * @brief MicroReactor uFlow coroutine macros
 *
 * Implements stackless coroutines using Duff's Device technique.
 * All cross-yield variables must be stored in entity scratchpad.
 */

#ifndef UR_FLOW_H
#define UR_FLOW_H

#include "ur_types.h"
#include "ur_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * uFlow Coroutine Macros
 *
 * Usage:
 *   uint16_t my_flow_action(ur_entity_t *ent, const ur_signal_t *sig) {
 *       UR_FLOW_BEGIN(ent);
 *
 *       // Initialize scratchpad
 *       UR_SCRATCH(ent, my_scratch_t)->counter = 0;
 *
 *       while (1) {
 *           UR_AWAIT_SIGNAL(ent, SIG_MY_EVENT);
 *           // sig is now the received signal
 *
 *           UR_AWAIT_TIME(ent, 1000); // Wait 1 second
 *
 *           UR_YIELD(ent); // Yield and resume on any signal
 *       }
 *
 *       UR_FLOW_END(ent);
 *   }
 *
 * Constraints:
 * - No nested switch statements inside flow actions
 * - All variables that must persist across yields go in scratchpad
 * - After UR_AWAIT_SIGNAL, the 'sig' parameter contains the received signal
 * ========================================================================== */

/**
 * @brief Begin a uFlow coroutine
 *
 * Must be the first statement in a flow action function.
 * Sets up the Duff's device switch and marks flow as running.
 */
#define UR_FLOW_BEGIN(ent) \
    do { \
        (ent)->flags |= UR_FLAG_FLOW_RUNNING; \
        switch ((ent)->flow_line) { \
        case 0:

/**
 * @brief End a uFlow coroutine
 *
 * Must be the last statement in a flow action function.
 * Resets flow state and returns 0 (stay in state).
 */
#define UR_FLOW_END(ent) \
        } \
        (ent)->flow_line = 0; \
        (ent)->flags &= ~UR_FLAG_FLOW_RUNNING; \
    } while (0); \
    return 0

/**
 * @brief Yield execution and resume on next signal
 *
 * Saves current position and returns. On next signal,
 * execution resumes after this macro.
 */
#define UR_YIELD(ent) \
    do { \
        (ent)->flow_line = __LINE__; \
        return 0; \
        case __LINE__:; \
    } while (0)

/**
 * @brief Wait for a specific signal
 *
 * Suspends until the specified signal is received.
 * After resuming, 'sig' parameter contains the received signal.
 *
 * @param ent       Entity pointer
 * @param sig_id    Signal ID to wait for
 */
#define UR_AWAIT_SIGNAL(ent, sig_id) \
    do { \
        (ent)->flow_wait_sig = (sig_id); \
        (ent)->flow_line = __LINE__; \
        return 0; \
        case __LINE__: \
        if (sig->id != (ent)->flow_wait_sig) { \
            return 0; \
        } \
        (ent)->flow_wait_sig = SIG_NONE; \
    } while (0)

/**
 * @brief Wait for any of multiple signals
 *
 * Resumes when any signal in the list is received.
 *
 * @param ent   Entity pointer
 * @param ...   Signal IDs (up to 4)
 */
#define UR_AWAIT_ANY(ent, ...) \
    do { \
        static const uint16_t _ur_sigs[] = { __VA_ARGS__, SIG_NONE }; \
        (ent)->flow_line = __LINE__; \
        return 0; \
        case __LINE__: \
        { \
            bool _ur_match = false; \
            for (int _ur_i = 0; _ur_sigs[_ur_i] != SIG_NONE; _ur_i++) { \
                if (sig->id == _ur_sigs[_ur_i]) { \
                    _ur_match = true; \
                    break; \
                } \
            } \
            if (!_ur_match) return 0; \
        } \
    } while (0)

/**
 * @brief Wait for a time duration
 *
 * Suspends for the specified duration. Internally waits for
 * SIG_SYS_TICK signals and checks elapsed time.
 *
 * @param ent       Entity pointer
 * @param ms        Duration in milliseconds
 */
#define UR_AWAIT_TIME(ent, ms) \
    do { \
        (ent)->flow_wait_until = ur_get_time_ms() + (ms); \
        (ent)->flow_line = __LINE__; \
        return 0; \
        case __LINE__: \
        if (ur_get_time_ms() < (ent)->flow_wait_until) { \
            return 0; \
        } \
    } while (0)

/**
 * @brief Wait until a condition is true
 *
 * Yields repeatedly until the condition evaluates to true.
 *
 * @param ent       Entity pointer
 * @param cond      Condition expression
 */
#define UR_AWAIT_COND(ent, cond) \
    do { \
        (ent)->flow_line = __LINE__; \
        case __LINE__: \
        if (!(cond)) { \
            return 0; \
        } \
    } while (0)

/**
 * @brief Emit a signal and yield
 *
 * Convenience macro for emit-then-yield pattern.
 *
 * @param ent       Entity pointer
 * @param target    Target entity pointer
 * @param signal    Signal to emit
 */
#define UR_EMIT_YIELD(ent, target, signal) \
    do { \
        ur_emit((target), (signal)); \
        UR_YIELD(ent); \
    } while (0)

/**
 * @brief Transition to another state and exit flow
 *
 * @param state_id  Target state ID
 */
#define UR_FLOW_GOTO(ent, state_id) \
    do { \
        (ent)->flow_line = 0; \
        (ent)->flags &= ~UR_FLAG_FLOW_RUNNING; \
        return (state_id); \
    } while (0)

/**
 * @brief Reset flow to beginning
 *
 * Restarts the coroutine from the top.
 */
#define UR_FLOW_RESET(ent) \
    do { \
        (ent)->flow_line = 0; \
    } while (0)

/* ============================================================================
 * Scratchpad Helpers
 * ========================================================================== */

/**
 * @brief Get typed pointer to scratchpad
 *
 * Usage:
 *   typedef struct { int counter; float value; } my_scratch_t;
 *   my_scratch_t *s = UR_SCRATCH_PTR(ent, my_scratch_t);
 *   s->counter++;
 */
#define UR_SCRATCH_PTR(ent, type) ((type *)((ent)->scratch))

/**
 * @brief Zero-initialize scratchpad
 */
#define UR_SCRATCH_CLEAR(ent) \
    memset((ent)->scratch, 0, UR_CFG_SCRATCHPAD_SIZE)

/**
 * @brief Get scratchpad size
 */
#define UR_SCRATCH_SIZE() UR_CFG_SCRATCHPAD_SIZE

/**
 * @brief Static assert that scratch type fits
 */
#define UR_SCRATCH_STATIC_ASSERT(type) \
    _Static_assert(sizeof(type) <= UR_CFG_SCRATCHPAD_SIZE, \
                   "Scratch type too large for scratchpad")

/* ============================================================================
 * Flow State Queries
 * ========================================================================== */

/**
 * @brief Check if flow is currently running
 */
#define UR_FLOW_IS_RUNNING(ent) \
    (((ent)->flags & UR_FLAG_FLOW_RUNNING) != 0)

/**
 * @brief Check if flow is waiting for a signal
 */
#define UR_FLOW_IS_WAITING_SIGNAL(ent) \
    ((ent)->flow_wait_sig != SIG_NONE)

/**
 * @brief Check if flow is in a time wait
 */
#define UR_FLOW_IS_WAITING_TIME(ent) \
    ((ent)->flow_wait_until > 0 && ur_get_time_ms() < (ent)->flow_wait_until)

/**
 * @brief Get the signal ID being waited for
 */
#define UR_FLOW_GET_WAIT_SIGNAL(ent) \
    ((ent)->flow_wait_sig)

#ifdef __cplusplus
}
#endif

#endif /* UR_FLOW_H */
