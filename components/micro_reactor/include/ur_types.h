/**
 * @file ur_types.h
 * @brief MicroReactor core type definitions
 *
 * All structures are 4-byte aligned for efficient memory access.
 * Zero dynamic memory allocation - all storage is statically allocated.
 */

#ifndef UR_TYPES_H
#define UR_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ur_config.h"

#if UR_CFG_USE_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Forward Declarations
 * ========================================================================== */

typedef struct ur_entity_s ur_entity_t;
typedef struct ur_signal_s ur_signal_t;
typedef struct ur_rule_s ur_rule_t;
typedef struct ur_state_def_s ur_state_def_t;
typedef struct ur_mixin_s ur_mixin_t;

/* ============================================================================
 * Error Codes
 * ========================================================================== */

typedef enum {
    UR_OK = 0,              /**< Success */
    UR_ERR_INVALID_ARG,     /**< Invalid argument */
    UR_ERR_NO_MEMORY,       /**< No memory available (static pool exhausted) */
    UR_ERR_QUEUE_FULL,      /**< Signal queue is full */
    UR_ERR_NOT_FOUND,       /**< Entity/state/rule not found */
    UR_ERR_INVALID_STATE,   /**< Invalid state transition */
    UR_ERR_TIMEOUT,         /**< Operation timed out */
    UR_ERR_ALREADY_EXISTS,  /**< Item already exists */
    UR_ERR_DISABLED,        /**< Feature disabled */
} ur_err_t;

/* ============================================================================
 * System Signal IDs
 * ========================================================================== */

typedef enum {
    /* Reserved system signals (0x0000 - 0x00FF) */
    SIG_NONE = 0x0000,          /**< No signal / null */
    SIG_SYS_INIT = 0x0001,      /**< Entity initialization */
    SIG_SYS_ENTRY = 0x0002,     /**< State entry */
    SIG_SYS_EXIT = 0x0003,      /**< State exit */
    SIG_SYS_TICK = 0x0004,      /**< Periodic tick */
    SIG_SYS_TIMEOUT = 0x0005,   /**< Timer timeout */
    SIG_SYS_DYING = 0x0006,     /**< Entity dying (supervisor) */
    SIG_SYS_REVIVE = 0x0007,    /**< Entity revive request (supervisor) */
    SIG_SYS_RESET = 0x0008,     /**< Soft reset request */
    SIG_SYS_SUSPEND = 0x0009,   /**< Suspend entity */
    SIG_SYS_RESUME = 0x000A,    /**< Resume entity */

    /* User signals start here */
    SIG_USER_BASE = 0x0100,     /**< First user-defined signal ID */
} ur_sys_signal_t;

/* ============================================================================
 * Entity Flags
 * ========================================================================== */

typedef enum {
    UR_FLAG_NONE = 0x00,
    UR_FLAG_ACTIVE = 0x01,          /**< Entity is active and processing */
    UR_FLAG_SUSPENDED = 0x02,       /**< Entity is suspended */
    UR_FLAG_FLOW_RUNNING = 0x04,    /**< uFlow coroutine is active */
    UR_FLAG_SUPERVISED = 0x08,      /**< Entity is under supervisor */
    UR_FLAG_SUPERVISOR = 0x10,      /**< Entity is a supervisor */
} ur_entity_flags_t;

/* ============================================================================
 * Signal Structure (20 bytes, 4-byte aligned)
 * ========================================================================== */

/**
 * @brief Signal structure for inter-entity communication
 *
 * Layout:
 * - id:        Signal identifier (16-bit)
 * - src_id:    Source entity ID (16-bit)
 * - payload:   Inline payload data
 * - ptr:       Pointer to external data (must be static/stack)
 * - timestamp: Signal creation time (microseconds)
 * - _reserved: Padding for alignment
 */
struct ur_signal_s {
    uint16_t id;                            /**< Signal identifier */
    uint16_t src_id;                        /**< Source entity ID */
    union {
        uint8_t  u8[UR_CFG_SIGNAL_PAYLOAD_SIZE];
        uint16_t u16[UR_CFG_SIGNAL_PAYLOAD_SIZE / 2];
        uint32_t u32[UR_CFG_SIGNAL_PAYLOAD_SIZE / 4];
        int8_t   i8[UR_CFG_SIGNAL_PAYLOAD_SIZE];
        int16_t  i16[UR_CFG_SIGNAL_PAYLOAD_SIZE / 2];
        int32_t  i32[UR_CFG_SIGNAL_PAYLOAD_SIZE / 4];
        float    f32;
    } payload;                              /**< Inline payload */
    void *ptr;                              /**< Pointer to external data */
    uint32_t timestamp;                     /**< Timestamp in ms (truncated) */
    uint32_t _reserved;                     /**< Reserved for alignment */
} __attribute__((aligned(4)));

/* ============================================================================
 * Action Function Type
 * ========================================================================== */

/**
 * @brief Action function signature
 * @param ent   Target entity
 * @param sig   Received signal
 * @return Next state ID, or 0 to stay in current state
 */
typedef uint16_t (*ur_action_fn_t)(ur_entity_t *ent, const ur_signal_t *sig);

/* ============================================================================
 * Transition Rule
 * ========================================================================== */

/**
 * @brief State transition rule
 *
 * Defines what happens when a signal is received in a particular state.
 */
struct ur_rule_s {
    uint16_t signal_id;         /**< Signal that triggers this rule */
    uint16_t next_state;        /**< Target state (0 = no transition) */
    ur_action_fn_t action;      /**< Action to execute (can be NULL) */
} __attribute__((aligned(4)));

/* ============================================================================
 * State Definition
 * ========================================================================== */

/**
 * @brief State definition with optional HSM parent
 */
struct ur_state_def_s {
    uint16_t id;                /**< State identifier */
    uint16_t parent_id;         /**< Parent state ID (0 = none, for HSM) */
    ur_action_fn_t on_entry;    /**< Entry action (optional) */
    ur_action_fn_t on_exit;     /**< Exit action (optional) */
    const ur_rule_t *rules;     /**< Pointer to rules array */
    uint8_t rule_count;         /**< Number of rules */
    uint8_t _reserved[3];       /**< Padding */
} __attribute__((aligned(4)));

/* ============================================================================
 * Mixin Definition
 * ========================================================================== */

/**
 * @brief Mixin for state-agnostic signal handling
 *
 * Mixins are checked after main state rules but before HSM bubble-up.
 */
struct ur_mixin_s {
    const char *name;           /**< Mixin name (for debugging) */
    const ur_rule_t *rules;     /**< Pointer to rules array */
    uint8_t rule_count;         /**< Number of rules */
    uint8_t priority;           /**< Lookup priority (lower = first) */
    uint8_t _reserved[2];       /**< Padding */
} __attribute__((aligned(4)));

/* ============================================================================
 * Middleware Types
 * ========================================================================== */

/**
 * @brief Middleware result codes
 */
typedef enum {
    UR_MW_CONTINUE = 0,     /**< Continue to next middleware */
    UR_MW_HANDLED,          /**< Signal handled, stop processing */
    UR_MW_FILTERED,         /**< Signal filtered out (dropped) */
    UR_MW_TRANSFORM,        /**< Signal transformed, continue */
} ur_mw_result_t;

/**
 * @brief Middleware function signature
 * @param ent   Target entity
 * @param sig   Signal being processed (can be modified)
 * @param ctx   Middleware-specific context
 * @return Middleware result code
 */
typedef ur_mw_result_t (*ur_middleware_fn_t)(ur_entity_t *ent,
                                             ur_signal_t *sig,
                                             void *ctx);

/**
 * @brief Middleware registration entry
 */
typedef struct {
    ur_middleware_fn_t fn;      /**< Middleware function */
    void *ctx;                  /**< Context pointer */
    uint8_t priority;           /**< Execution priority (lower = first) */
    uint8_t enabled;            /**< Enable flag */
    uint8_t _reserved[2];       /**< Padding */
} ur_middleware_t;

/* ============================================================================
 * Entity Control Block
 * ========================================================================== */

/**
 * @brief Entity control block
 *
 * Contains all state for a single reactive entity.
 */
struct ur_entity_s {
    /* Identification */
    uint16_t id;                /**< Entity ID */
    uint16_t current_state;     /**< Current state ID */
    uint8_t flags;              /**< Entity flags */
    uint8_t mixin_count;        /**< Number of attached mixins */
    uint8_t middleware_count;   /**< Number of middleware in chain */
    uint8_t _reserved;          /**< Padding */

    /* State machine */
    const ur_state_def_t *states;           /**< State definitions array */
    uint8_t state_count;                    /**< Number of states */
    uint8_t initial_state;                  /**< Initial state ID */
    uint8_t _pad1[2];                       /**< Padding */

    /* Mixins */
#if UR_CFG_MAX_MIXINS_PER_ENTITY > 0
    const ur_mixin_t *mixins[UR_CFG_MAX_MIXINS_PER_ENTITY];
#endif

    /* Middleware chain */
#if UR_CFG_MAX_MIDDLEWARE > 0
    ur_middleware_t middleware[UR_CFG_MAX_MIDDLEWARE];
#endif

    /* uFlow coroutine state */
    uint16_t flow_line;         /**< Duff's device line number */
    uint16_t flow_wait_sig;     /**< Signal ID we're waiting for */
    uint32_t flow_wait_until;   /**< Timeout timestamp */

    /* Scratchpad for uFlow local variables */
    uint8_t scratch[UR_CFG_SCRATCHPAD_SIZE] __attribute__((aligned(4)));

    /* Supervisor support */
#if UR_CFG_SUPERVISOR_ENABLE
    uint16_t supervisor_id;     /**< Parent supervisor entity ID */
    uint16_t _pad2;             /**< Padding */
#endif

    /* User data pointer */
    void *user_data;            /**< Application-specific data */

    /* FreeRTOS resources */
#if UR_CFG_USE_FREERTOS
    QueueHandle_t inbox;        /**< Signal inbox queue */
    StaticQueue_t inbox_static; /**< Static queue storage */
    uint8_t inbox_buffer[UR_CFG_INBOX_SIZE * sizeof(ur_signal_t)];
#endif

    /* Name for debugging */
    const char *name;           /**< Entity name (optional) */
} __attribute__((aligned(4)));

/* ============================================================================
 * Data Pipe Structure
 * ========================================================================== */

#if UR_CFG_PIPE_ENABLE

/**
 * @brief Data pipe for high-throughput streaming
 */
typedef struct {
    StreamBufferHandle_t handle;    /**< FreeRTOS stream buffer */
    StaticStreamBuffer_t static_sb; /**< Static storage */
    uint8_t *buffer;                /**< Data buffer pointer */
    size_t buffer_size;             /**< Buffer size */
    size_t trigger_level;           /**< Trigger level for blocking reads */
} ur_pipe_t;

#endif /* UR_CFG_PIPE_ENABLE */

/* ============================================================================
 * Wormhole Types
 * ========================================================================== */

#if UR_CFG_WORMHOLE_ENABLE

/**
 * @brief Wormhole frame header
 */
#define UR_WORMHOLE_SYNC_BYTE   0xAA
#define UR_WORMHOLE_FRAME_SIZE  10

/**
 * @brief Wormhole route entry
 */
typedef struct {
    uint16_t entity_id;     /**< Local entity ID */
    uint16_t remote_id;     /**< Remote entity ID */
    uint8_t channel;        /**< UART channel */
    uint8_t flags;          /**< Route flags */
    uint8_t _reserved[2];   /**< Padding */
} ur_wormhole_route_t;

/**
 * @brief Wormhole frame structure (10 bytes)
 */
typedef struct {
    uint8_t sync;           /**< Sync byte (0xAA) */
    uint16_t src_id;        /**< Source entity ID */
    uint16_t sig_id;        /**< Signal ID */
    uint32_t payload;       /**< 4-byte payload */
    uint8_t crc8;           /**< CRC8 checksum */
} __attribute__((packed)) ur_wormhole_frame_t;

#endif /* UR_CFG_WORMHOLE_ENABLE */

/* ============================================================================
 * Panic/Black Box Types
 * ========================================================================== */

#if UR_CFG_PANIC_ENABLE

/**
 * @brief Black box entry for signal history
 */
typedef struct {
    uint16_t entity_id;     /**< Target entity ID */
    uint16_t signal_id;     /**< Signal ID */
    uint16_t src_id;        /**< Source entity ID */
    uint16_t state;         /**< Entity state at time of signal */
    uint32_t timestamp;     /**< Timestamp */
} ur_blackbox_entry_t;

/**
 * @brief Panic hook function type
 */
typedef void (*ur_panic_hook_t)(const char *reason,
                                 const ur_blackbox_entry_t *history,
                                 size_t history_count);

#endif /* UR_CFG_PANIC_ENABLE */

/* ============================================================================
 * Entity Configuration (for initialization)
 * ========================================================================== */

/**
 * @brief Entity configuration structure
 */
typedef struct {
    uint16_t id;                    /**< Entity ID */
    const char *name;               /**< Entity name (optional) */
    const ur_state_def_t *states;   /**< State definitions */
    uint8_t state_count;            /**< Number of states */
    uint8_t initial_state;          /**< Initial state ID */
    void *user_data;                /**< User data pointer */
} ur_entity_config_t;

/* ============================================================================
 * Utility Macros
 * ========================================================================== */

/**
 * @brief Create a signal with inline u32 payload
 */
#define UR_SIGNAL_U32(id, src, val) \
    ((ur_signal_t){ .id = (id), .src_id = (src), .payload.u32[0] = (val) })

/**
 * @brief Create a signal with pointer payload
 */
#define UR_SIGNAL_PTR(id, src, p) \
    ((ur_signal_t){ .id = (id), .src_id = (src), .ptr = (p) })

/**
 * @brief Define a transition rule
 */
#define UR_RULE(sig, next, act) \
    { .signal_id = (sig), .next_state = (next), .action = (act) }

/**
 * @brief End of rules array marker
 */
#define UR_RULE_END \
    { .signal_id = SIG_NONE, .next_state = 0, .action = NULL }

/**
 * @brief Define a state
 */
#define UR_STATE(state_id, parent, entry, exit, rule_array) \
    { \
        .id = (state_id), \
        .parent_id = (parent), \
        .on_entry = (entry), \
        .on_exit = (exit), \
        .rules = (rule_array), \
        .rule_count = sizeof(rule_array) / sizeof((rule_array)[0]) - 1 \
    }

/**
 * @brief Get scratchpad as typed pointer
 */
#define UR_SCRATCH(ent, type) ((type *)((ent)->scratch))

/**
 * @brief Check if entity ID is valid
 */
#define UR_VALID_ENTITY_ID(id) ((id) > 0 && (id) <= UR_CFG_MAX_ENTITIES)

#ifdef __cplusplus
}
#endif

#endif /* UR_TYPES_H */
