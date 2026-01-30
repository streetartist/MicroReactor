/**
 * @file ur_trace.h
 * @brief MicroReactor Performance Tracing (Instrumentation)
 *
 * Provides execution tracing for debugging timing issues:
 * - Entry/exit timestamps for dispatches
 * - Signal flow visualization
 * - State transition tracking
 * - Multiple output backends (RTT, UART, buffer)
 *
 * Output formats:
 * - Perfetto JSON (Chrome tracing)
 * - SystemView compatible
 * - Custom binary (minimal overhead)
 *
 * Zero overhead when disabled (compile-time).
 */

#ifndef UR_TRACE_H
#define UR_TRACE_H

#include "ur_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration
 * ========================================================================== */

#ifndef UR_CFG_TRACE_ENABLE
#ifdef CONFIG_UR_TRACE_ENABLE
#define UR_CFG_TRACE_ENABLE         CONFIG_UR_TRACE_ENABLE
#else
#define UR_CFG_TRACE_ENABLE         0
#endif
#endif

#ifndef UR_CFG_TRACE_BUFFER_SIZE
#ifdef CONFIG_UR_TRACE_BUFFER_SIZE
#define UR_CFG_TRACE_BUFFER_SIZE    CONFIG_UR_TRACE_BUFFER_SIZE
#else
#define UR_CFG_TRACE_BUFFER_SIZE    4096
#endif
#endif

#ifndef UR_CFG_TRACE_MAX_ENTRIES
#ifdef CONFIG_UR_TRACE_MAX_ENTRIES
#define UR_CFG_TRACE_MAX_ENTRIES    CONFIG_UR_TRACE_MAX_ENTRIES
#else
#define UR_CFG_TRACE_MAX_ENTRIES    256
#endif
#endif

/* ============================================================================
 * Trace Macros (Zero overhead when disabled)
 * ========================================================================== */

#if UR_CFG_TRACE_ENABLE

#define UR_TRACE_INIT()                 ur_trace_init()
#define UR_TRACE_START(ent_id, sig_id)  ur_trace_dispatch_start(ent_id, sig_id)
#define UR_TRACE_END(ent_id, sig_id)    ur_trace_dispatch_end(ent_id, sig_id)
#define UR_TRACE_TRANSITION(ent_id, from, to) \
                                        ur_trace_state_transition(ent_id, from, to)
#define UR_TRACE_SIGNAL(src, dst, sig)  ur_trace_signal_flow(src, dst, sig)
#define UR_TRACE_MARK(label)            ur_trace_marker(label)
#define UR_TRACE_VALUE(name, val)       ur_trace_counter(name, val)
#define UR_TRACE_FLUSH()                ur_trace_flush()

#else

#define UR_TRACE_INIT()                 ((void)0)
#define UR_TRACE_START(ent_id, sig_id)  ((void)0)
#define UR_TRACE_END(ent_id, sig_id)    ((void)0)
#define UR_TRACE_TRANSITION(ent_id, from, to) ((void)0)
#define UR_TRACE_SIGNAL(src, dst, sig)  ((void)0)
#define UR_TRACE_MARK(label)            ((void)0)
#define UR_TRACE_VALUE(name, val)       ((void)0)
#define UR_TRACE_FLUSH()                ((void)0)

#endif /* UR_CFG_TRACE_ENABLE */

#if UR_CFG_TRACE_ENABLE

/* ============================================================================
 * Types
 * ========================================================================== */

/**
 * @brief Trace event types
 */
typedef enum {
    UR_TRACE_EVT_DISPATCH_START = 0,    /**< Dispatch began */
    UR_TRACE_EVT_DISPATCH_END,          /**< Dispatch ended */
    UR_TRACE_EVT_STATE_CHANGE,          /**< State transition */
    UR_TRACE_EVT_SIGNAL_EMIT,           /**< Signal emitted */
    UR_TRACE_EVT_SIGNAL_RECV,           /**< Signal received */
    UR_TRACE_EVT_MARKER,                /**< User marker */
    UR_TRACE_EVT_COUNTER,               /**< Counter value */
    UR_TRACE_EVT_ISR_ENTER,             /**< ISR entered */
    UR_TRACE_EVT_ISR_EXIT,              /**< ISR exited */
    UR_TRACE_EVT_IDLE_ENTER,            /**< Entered idle */
    UR_TRACE_EVT_IDLE_EXIT,             /**< Exited idle */
} ur_trace_event_type_t;

/**
 * @brief Trace event entry (16 bytes, optimized for cache)
 */
typedef struct {
    uint32_t timestamp_us;      /**< Timestamp in microseconds */
    uint16_t entity_id;         /**< Entity ID */
    uint8_t event_type;         /**< ur_trace_event_type_t */
    uint8_t flags;              /**< Event flags */
    union {
        struct {
            uint16_t signal_id;
            uint16_t src_id;
        } signal;
        struct {
            uint16_t from_state;
            uint16_t to_state;
        } transition;
        struct {
            uint16_t marker_id;
            uint16_t _pad;
        } marker;
        uint32_t counter_value;
    } data;
} __attribute__((packed, aligned(4))) ur_trace_event_t;

/**
 * @brief Trace output format
 */
typedef enum {
    UR_TRACE_FMT_BINARY = 0,    /**< Raw binary events */
    UR_TRACE_FMT_PERFETTO,      /**< Perfetto JSON */
    UR_TRACE_FMT_TEXT,          /**< Human-readable text */
} ur_trace_format_t;

/**
 * @brief Trace output backend callbacks
 */
typedef struct {
    /**
     * @brief Initialize backend
     * @return UR_OK on success
     */
    ur_err_t (*init)(void);

    /**
     * @brief Write trace data
     * @param data  Data buffer
     * @param len   Data length
     * @return Bytes written
     */
    size_t (*write)(const void *data, size_t len);

    /**
     * @brief Flush output
     */
    void (*flush)(void);

    /**
     * @brief Deinitialize backend
     */
    void (*deinit)(void);
} ur_trace_backend_t;

/**
 * @brief Trace statistics
 */
typedef struct {
    uint32_t events_recorded;   /**< Total events recorded */
    uint32_t events_dropped;    /**< Events dropped (buffer full) */
    uint32_t bytes_written;     /**< Bytes written to backend */
    uint32_t max_dispatch_us;   /**< Longest dispatch time */
    uint16_t max_dispatch_ent;  /**< Entity with longest dispatch */
    uint16_t max_dispatch_sig;  /**< Signal with longest dispatch */
} ur_trace_stats_t;

/* ============================================================================
 * Initialization
 * ========================================================================== */

/**
 * @brief Initialize tracing system
 *
 * @return UR_OK on success
 */
ur_err_t ur_trace_init(void);

/**
 * @brief Set trace output backend
 *
 * @param backend   Backend implementation
 * @return UR_OK on success
 */
ur_err_t ur_trace_set_backend(const ur_trace_backend_t *backend);

/**
 * @brief Set trace output format
 *
 * @param format    Output format
 */
void ur_trace_set_format(ur_trace_format_t format);

/**
 * @brief Start/stop tracing
 *
 * @param enable    Enable tracing
 */
void ur_trace_enable(bool enable);

/**
 * @brief Check if tracing is enabled
 *
 * @return true if enabled
 */
bool ur_trace_is_enabled(void);

/* ============================================================================
 * Core Tracing Functions
 * ========================================================================== */

/**
 * @brief Record dispatch start
 *
 * @param entity_id Entity being dispatched
 * @param signal_id Signal being processed
 */
void ur_trace_dispatch_start(uint16_t entity_id, uint16_t signal_id);

/**
 * @brief Record dispatch end
 *
 * @param entity_id Entity that was dispatched
 * @param signal_id Signal that was processed
 */
void ur_trace_dispatch_end(uint16_t entity_id, uint16_t signal_id);

/**
 * @brief Record state transition
 *
 * @param entity_id Entity transitioning
 * @param from_state Previous state
 * @param to_state New state
 */
void ur_trace_state_transition(uint16_t entity_id,
                                uint16_t from_state,
                                uint16_t to_state);

/**
 * @brief Record signal flow
 *
 * @param src_id    Source entity
 * @param dst_id    Destination entity
 * @param signal_id Signal ID
 */
void ur_trace_signal_flow(uint16_t src_id,
                           uint16_t dst_id,
                           uint16_t signal_id);

/**
 * @brief Record user marker
 *
 * @param label Marker label (static string)
 */
void ur_trace_marker(const char *label);

/**
 * @brief Record counter value
 *
 * @param name  Counter name (static string)
 * @param value Counter value
 */
void ur_trace_counter(const char *name, uint32_t value);

/**
 * @brief Record ISR entry
 *
 * @param isr_id ISR identifier
 */
void ur_trace_isr_enter(uint16_t isr_id);

/**
 * @brief Record ISR exit
 *
 * @param isr_id ISR identifier
 */
void ur_trace_isr_exit(uint16_t isr_id);

/**
 * @brief Record idle entry
 *
 * @param expected_ms Expected idle duration
 */
void ur_trace_idle_enter(uint32_t expected_ms);

/**
 * @brief Record idle exit
 *
 * @param actual_ms Actual idle duration
 */
void ur_trace_idle_exit(uint32_t actual_ms);

/* ============================================================================
 * Output Control
 * ========================================================================== */

/**
 * @brief Flush trace buffer to backend
 */
void ur_trace_flush(void);

/**
 * @brief Clear trace buffer
 */
void ur_trace_clear(void);

/**
 * @brief Get trace statistics
 *
 * @param stats Output statistics
 */
void ur_trace_get_stats(ur_trace_stats_t *stats);

/**
 * @brief Reset trace statistics
 */
void ur_trace_reset_stats(void);

/* ============================================================================
 * Export Functions
 * ========================================================================== */

/**
 * @brief Export trace buffer in specified format
 *
 * @param format    Output format
 * @param buffer    Output buffer
 * @param buf_size  Buffer size
 * @param out_len   Output: bytes written
 * @return UR_OK on success
 */
ur_err_t ur_trace_export(ur_trace_format_t format,
                          void *buffer,
                          size_t buf_size,
                          size_t *out_len);

/**
 * @brief Stream export (for large traces)
 *
 * Calls callback with chunks of data.
 *
 * @param format    Output format
 * @param callback  Callback for each chunk
 * @param ctx       User context
 * @return Total bytes exported
 */
typedef size_t (*ur_trace_export_cb_t)(const void *data, size_t len, void *ctx);

size_t ur_trace_export_stream(ur_trace_format_t format,
                               ur_trace_export_cb_t callback,
                               void *ctx);

/* ============================================================================
 * Entity Name Registration (for readable output)
 * ========================================================================== */

/**
 * @brief Register entity name for trace output
 *
 * @param entity_id Entity ID
 * @param name      Entity name
 */
void ur_trace_register_entity_name(uint16_t entity_id, const char *name);

/**
 * @brief Register signal name for trace output
 *
 * @param signal_id Signal ID
 * @param name      Signal name
 */
void ur_trace_register_signal_name(uint16_t signal_id, const char *name);

/**
 * @brief Register state name for trace output
 *
 * @param entity_id Entity ID
 * @param state_id  State ID
 * @param name      State name
 */
void ur_trace_register_state_name(uint16_t entity_id,
                                   uint16_t state_id,
                                   const char *name);

/* ============================================================================
 * Built-in Backends
 * ========================================================================== */

/**
 * @brief Ring buffer backend (in-memory)
 */
extern const ur_trace_backend_t ur_trace_backend_buffer;

/**
 * @brief UART output backend
 */
extern const ur_trace_backend_t ur_trace_backend_uart;

#if defined(CONFIG_IDF_TARGET) || defined(ESP_PLATFORM)
/**
 * @brief SEGGER RTT backend (if available)
 */
extern const ur_trace_backend_t ur_trace_backend_rtt;
#endif

#endif /* UR_CFG_TRACE_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* UR_TRACE_H */
