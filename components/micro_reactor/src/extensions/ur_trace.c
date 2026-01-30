/**
 * @file ur_trace.c
 * @brief MicroReactor Performance Tracing Implementation
 */

#include "ur_trace.h"
#include "ur_utils.h"
#include <string.h>
#include <stdio.h>

#if UR_CFG_TRACE_ENABLE

/* ============================================================================
 * Internal State
 * ========================================================================== */

typedef struct {
    uint16_t id;
    const char *name;
} name_entry_t;

#define MAX_NAME_ENTRIES 64

static struct {
    ur_trace_event_t events[UR_CFG_TRACE_MAX_ENTRIES];
    size_t event_head;
    size_t event_count;

    const ur_trace_backend_t *backend;
    ur_trace_format_t format;
    ur_trace_stats_t stats;

    /* Name registrations for readable output */
    name_entry_t entity_names[MAX_NAME_ENTRIES];
    name_entry_t signal_names[MAX_NAME_ENTRIES];
    size_t entity_name_count;
    size_t signal_name_count;

    bool enabled;
    bool initialized;

    /* Dispatch timing */
    uint32_t dispatch_start_us;
} g_trace = { 0 };

/* ============================================================================
 * Internal Functions
 * ========================================================================== */

static uint32_t get_time_us(void)
{
#if defined(CONFIG_IDF_TARGET) || defined(ESP_PLATFORM)
    return (uint32_t)(esp_timer_get_time());
#else
    return ur_get_time_ms() * 1000;
#endif
}

static void record_event(ur_trace_event_type_t type, uint16_t entity_id,
                         uint32_t data1, uint32_t data2)
{
    if (!g_trace.enabled) {
        return;
    }

    size_t idx = g_trace.event_head;

    g_trace.events[idx].timestamp_us = get_time_us();
    g_trace.events[idx].entity_id = entity_id;
    g_trace.events[idx].event_type = type;
    g_trace.events[idx].flags = 0;

    switch (type) {
        case UR_TRACE_EVT_DISPATCH_START:
        case UR_TRACE_EVT_DISPATCH_END:
        case UR_TRACE_EVT_SIGNAL_EMIT:
        case UR_TRACE_EVT_SIGNAL_RECV:
            g_trace.events[idx].data.signal.signal_id = (uint16_t)data1;
            g_trace.events[idx].data.signal.src_id = (uint16_t)data2;
            break;

        case UR_TRACE_EVT_STATE_CHANGE:
            g_trace.events[idx].data.transition.from_state = (uint16_t)data1;
            g_trace.events[idx].data.transition.to_state = (uint16_t)data2;
            break;

        case UR_TRACE_EVT_MARKER:
            g_trace.events[idx].data.marker.marker_id = (uint16_t)data1;
            break;

        case UR_TRACE_EVT_COUNTER:
            g_trace.events[idx].data.counter_value = data1;
            break;

        default:
            break;
    }

    g_trace.event_head = (g_trace.event_head + 1) % UR_CFG_TRACE_MAX_ENTRIES;

    if (g_trace.event_count < UR_CFG_TRACE_MAX_ENTRIES) {
        g_trace.event_count++;
    } else {
        g_trace.stats.events_dropped++;
    }

    g_trace.stats.events_recorded++;
}

static const char *find_entity_name(uint16_t id)
{
    for (size_t i = 0; i < g_trace.entity_name_count; i++) {
        if (g_trace.entity_names[i].id == id) {
            return g_trace.entity_names[i].name;
        }
    }
    return NULL;
}

static const char *find_signal_name(uint16_t id)
{
    for (size_t i = 0; i < g_trace.signal_name_count; i++) {
        if (g_trace.signal_names[i].id == id) {
            return g_trace.signal_names[i].name;
        }
    }
    return NULL;
}

/* ============================================================================
 * Initialization
 * ========================================================================== */

ur_err_t ur_trace_init(void)
{
    memset(&g_trace, 0, sizeof(g_trace));
    g_trace.initialized = true;
    g_trace.enabled = true;
    g_trace.format = UR_TRACE_FMT_TEXT;

    return UR_OK;
}

ur_err_t ur_trace_set_backend(const ur_trace_backend_t *backend)
{
    if (backend != NULL && backend->init != NULL) {
        ur_err_t err = backend->init();
        if (err != UR_OK) {
            return err;
        }
    }

    g_trace.backend = backend;
    return UR_OK;
}

void ur_trace_set_format(ur_trace_format_t format)
{
    g_trace.format = format;
}

void ur_trace_enable(bool enable)
{
    g_trace.enabled = enable;
}

bool ur_trace_is_enabled(void)
{
    return g_trace.enabled;
}

/* ============================================================================
 * Core Tracing Functions
 * ========================================================================== */

void ur_trace_dispatch_start(uint16_t entity_id, uint16_t signal_id)
{
    g_trace.dispatch_start_us = get_time_us();
    record_event(UR_TRACE_EVT_DISPATCH_START, entity_id, signal_id, 0);
}

void ur_trace_dispatch_end(uint16_t entity_id, uint16_t signal_id)
{
    record_event(UR_TRACE_EVT_DISPATCH_END, entity_id, signal_id, 0);

    /* Track max dispatch time */
    uint32_t duration = get_time_us() - g_trace.dispatch_start_us;
    if (duration > g_trace.stats.max_dispatch_us) {
        g_trace.stats.max_dispatch_us = duration;
        g_trace.stats.max_dispatch_ent = entity_id;
        g_trace.stats.max_dispatch_sig = signal_id;
    }
}

void ur_trace_state_transition(uint16_t entity_id,
                                uint16_t from_state,
                                uint16_t to_state)
{
    record_event(UR_TRACE_EVT_STATE_CHANGE, entity_id, from_state, to_state);
}

void ur_trace_signal_flow(uint16_t src_id,
                           uint16_t dst_id,
                           uint16_t signal_id)
{
    record_event(UR_TRACE_EVT_SIGNAL_EMIT, dst_id, signal_id, src_id);
}

void ur_trace_marker(const char *label)
{
    /* Simple hash for marker ID */
    uint16_t hash = 0;
    if (label != NULL) {
        while (*label) {
            hash = (hash << 5) - hash + *label++;
        }
    }
    record_event(UR_TRACE_EVT_MARKER, 0, hash, 0);
}

void ur_trace_counter(const char *name, uint32_t value)
{
    (void)name;
    record_event(UR_TRACE_EVT_COUNTER, 0, value, 0);
}

void ur_trace_isr_enter(uint16_t isr_id)
{
    record_event(UR_TRACE_EVT_ISR_ENTER, isr_id, 0, 0);
}

void ur_trace_isr_exit(uint16_t isr_id)
{
    record_event(UR_TRACE_EVT_ISR_EXIT, isr_id, 0, 0);
}

void ur_trace_idle_enter(uint32_t expected_ms)
{
    record_event(UR_TRACE_EVT_IDLE_ENTER, 0, expected_ms, 0);
}

void ur_trace_idle_exit(uint32_t actual_ms)
{
    record_event(UR_TRACE_EVT_IDLE_EXIT, 0, actual_ms, 0);
}

/* ============================================================================
 * Output Control
 * ========================================================================== */

void ur_trace_flush(void)
{
    if (g_trace.backend == NULL || g_trace.backend->write == NULL) {
        return;
    }

    /* Write all events to backend */
    for (size_t i = 0; i < g_trace.event_count; i++) {
        size_t idx = (g_trace.event_head - g_trace.event_count + i +
                      UR_CFG_TRACE_MAX_ENTRIES) % UR_CFG_TRACE_MAX_ENTRIES;

        size_t written = g_trace.backend->write(&g_trace.events[idx],
                                                 sizeof(ur_trace_event_t));
        g_trace.stats.bytes_written += written;
    }

    if (g_trace.backend->flush != NULL) {
        g_trace.backend->flush();
    }
}

void ur_trace_clear(void)
{
    g_trace.event_head = 0;
    g_trace.event_count = 0;
}

void ur_trace_get_stats(ur_trace_stats_t *stats)
{
    if (stats != NULL) {
        *stats = g_trace.stats;
    }
}

void ur_trace_reset_stats(void)
{
    memset(&g_trace.stats, 0, sizeof(g_trace.stats));
}

/* ============================================================================
 * Export Functions
 * ========================================================================== */

static const char *event_type_name(ur_trace_event_type_t type)
{
    static const char *names[] = {
        "DISPATCH_START",
        "DISPATCH_END",
        "STATE_CHANGE",
        "SIGNAL_EMIT",
        "SIGNAL_RECV",
        "MARKER",
        "COUNTER",
        "ISR_ENTER",
        "ISR_EXIT",
        "IDLE_ENTER",
        "IDLE_EXIT"
    };

    if (type < sizeof(names) / sizeof(names[0])) {
        return names[type];
    }
    return "UNKNOWN";
}

ur_err_t ur_trace_export(ur_trace_format_t format,
                          void *buffer,
                          size_t buf_size,
                          size_t *out_len)
{
    if (buffer == NULL || out_len == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    size_t pos = 0;

    switch (format) {
        case UR_TRACE_FMT_BINARY:
            /* Raw binary export */
            for (size_t i = 0; i < g_trace.event_count && pos + sizeof(ur_trace_event_t) <= buf_size; i++) {
                size_t idx = (g_trace.event_head - g_trace.event_count + i +
                              UR_CFG_TRACE_MAX_ENTRIES) % UR_CFG_TRACE_MAX_ENTRIES;
                memcpy((uint8_t *)buffer + pos, &g_trace.events[idx], sizeof(ur_trace_event_t));
                pos += sizeof(ur_trace_event_t);
            }
            break;

        case UR_TRACE_FMT_TEXT: {
            /* Human-readable text */
            char *out = (char *)buffer;
            for (size_t i = 0; i < g_trace.event_count && pos < buf_size - 100; i++) {
                size_t idx = (g_trace.event_head - g_trace.event_count + i +
                              UR_CFG_TRACE_MAX_ENTRIES) % UR_CFG_TRACE_MAX_ENTRIES;
                const ur_trace_event_t *evt = &g_trace.events[idx];

                const char *ent_name = find_entity_name(evt->entity_id);
                char ent_str[32];
                if (ent_name) {
                    snprintf(ent_str, sizeof(ent_str), "%s", ent_name);
                } else {
                    snprintf(ent_str, sizeof(ent_str), "E%d", evt->entity_id);
                }

                int written = 0;
                switch (evt->event_type) {
                    case UR_TRACE_EVT_DISPATCH_START:
                    case UR_TRACE_EVT_DISPATCH_END: {
                        const char *sig_name = find_signal_name(evt->data.signal.signal_id);
                        written = snprintf(&out[pos], buf_size - pos,
                                          "[%lu] %s %s sig=0x%04X%s%s\n",
                                          (unsigned long)evt->timestamp_us,
                                          event_type_name(evt->event_type),
                                          ent_str,
                                          evt->data.signal.signal_id,
                                          sig_name ? " (" : "",
                                          sig_name ? sig_name : "");
                        break;
                    }
                    case UR_TRACE_EVT_STATE_CHANGE:
                        written = snprintf(&out[pos], buf_size - pos,
                                          "[%lu] %s %s %d -> %d\n",
                                          (unsigned long)evt->timestamp_us,
                                          event_type_name(evt->event_type),
                                          ent_str,
                                          evt->data.transition.from_state,
                                          evt->data.transition.to_state);
                        break;
                    default:
                        written = snprintf(&out[pos], buf_size - pos,
                                          "[%lu] %s %s\n",
                                          (unsigned long)evt->timestamp_us,
                                          event_type_name(evt->event_type),
                                          ent_str);
                        break;
                }
                if (written > 0) {
                    pos += written;
                }
            }
            break;
        }

        case UR_TRACE_FMT_PERFETTO: {
            /* Perfetto/Chrome Trace JSON format */
            char *out = (char *)buffer;
            int written = snprintf(&out[pos], buf_size - pos, "{\"traceEvents\":[");
            if (written > 0) pos += written;

            bool first = true;
            for (size_t i = 0; i < g_trace.event_count && pos < buf_size - 200; i++) {
                size_t idx = (g_trace.event_head - g_trace.event_count + i +
                              UR_CFG_TRACE_MAX_ENTRIES) % UR_CFG_TRACE_MAX_ENTRIES;
                const ur_trace_event_t *evt = &g_trace.events[idx];

                const char *ent_name = find_entity_name(evt->entity_id);
                char name[64];
                if (ent_name) {
                    snprintf(name, sizeof(name), "%s", ent_name);
                } else {
                    snprintf(name, sizeof(name), "Entity_%d", evt->entity_id);
                }

                char phase = 'i';  /* Instant event */
                if (evt->event_type == UR_TRACE_EVT_DISPATCH_START) phase = 'B';
                else if (evt->event_type == UR_TRACE_EVT_DISPATCH_END) phase = 'E';

                written = snprintf(&out[pos], buf_size - pos,
                                  "%s{\"name\":\"%s\",\"cat\":\"%s\",\"ph\":\"%c\","
                                  "\"ts\":%lu,\"pid\":1,\"tid\":%d}",
                                  first ? "" : ",",
                                  event_type_name(evt->event_type),
                                  name,
                                  phase,
                                  (unsigned long)evt->timestamp_us,
                                  evt->entity_id);
                if (written > 0) {
                    pos += written;
                    first = false;
                }
            }

            written = snprintf(&out[pos], buf_size - pos, "]}");
            if (written > 0) pos += written;
            break;
        }
    }

    *out_len = pos;
    return UR_OK;
}

size_t ur_trace_export_stream(ur_trace_format_t format,
                               ur_trace_export_cb_t callback,
                               void *ctx)
{
    if (callback == NULL) {
        return 0;
    }

    /* Export in chunks */
    char buffer[512];
    size_t total = 0;
    size_t out_len;

    if (ur_trace_export(format, buffer, sizeof(buffer), &out_len) == UR_OK) {
        total += callback(buffer, out_len, ctx);
    }

    return total;
}

/* ============================================================================
 * Name Registration
 * ========================================================================== */

void ur_trace_register_entity_name(uint16_t entity_id, const char *name)
{
    if (name == NULL || g_trace.entity_name_count >= MAX_NAME_ENTRIES) {
        return;
    }

    /* Check for existing */
    for (size_t i = 0; i < g_trace.entity_name_count; i++) {
        if (g_trace.entity_names[i].id == entity_id) {
            g_trace.entity_names[i].name = name;
            return;
        }
    }

    g_trace.entity_names[g_trace.entity_name_count].id = entity_id;
    g_trace.entity_names[g_trace.entity_name_count].name = name;
    g_trace.entity_name_count++;
}

void ur_trace_register_signal_name(uint16_t signal_id, const char *name)
{
    if (name == NULL || g_trace.signal_name_count >= MAX_NAME_ENTRIES) {
        return;
    }

    /* Check for existing */
    for (size_t i = 0; i < g_trace.signal_name_count; i++) {
        if (g_trace.signal_names[i].id == signal_id) {
            g_trace.signal_names[i].name = name;
            return;
        }
    }

    g_trace.signal_names[g_trace.signal_name_count].id = signal_id;
    g_trace.signal_names[g_trace.signal_name_count].name = name;
    g_trace.signal_name_count++;
}

void ur_trace_register_state_name(uint16_t entity_id,
                                   uint16_t state_id,
                                   const char *name)
{
    /* State names could be stored similarly, but for simplicity,
     * we'll skip this for now as it would require a more complex structure */
    (void)entity_id;
    (void)state_id;
    (void)name;
}

/* ============================================================================
 * Built-in Backends
 * ========================================================================== */

/* Ring buffer backend (already in g_trace.events) */
static ur_err_t buffer_init(void) { return UR_OK; }
static size_t buffer_write(const void *data, size_t len) { (void)data; return len; }
static void buffer_flush(void) {}
static void buffer_deinit(void) {}

const ur_trace_backend_t ur_trace_backend_buffer = {
    .init = buffer_init,
    .write = buffer_write,
    .flush = buffer_flush,
    .deinit = buffer_deinit,
};

/* UART backend */
#if UR_CFG_USE_FREERTOS
#include "driver/uart.h"

static size_t uart_write(const void *data, size_t len)
{
    return uart_write_bytes(UART_NUM_0, (const char *)data, len);
}

static void uart_flush_fn(void)
{
    uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(100));
}

const ur_trace_backend_t ur_trace_backend_uart = {
    .init = buffer_init,
    .write = uart_write,
    .flush = uart_flush_fn,
    .deinit = buffer_deinit,
};
#else
const ur_trace_backend_t ur_trace_backend_uart = {
    .init = buffer_init,
    .write = buffer_write,
    .flush = buffer_flush,
    .deinit = buffer_deinit,
};
#endif

#endif /* UR_CFG_TRACE_ENABLE */
