/**
 * @file ur_config.h
 * @brief MicroReactor compile-time configuration macros
 *
 * This file provides default values that can be overridden via Kconfig
 * (sdkconfig) or by defining macros before including this header.
 */

#ifndef UR_CONFIG_H
#define UR_CONFIG_H

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Core Configuration
 * ========================================================================== */

#ifndef UR_CFG_MAX_ENTITIES
#ifdef CONFIG_UR_MAX_ENTITIES
#define UR_CFG_MAX_ENTITIES         CONFIG_UR_MAX_ENTITIES
#else
#define UR_CFG_MAX_ENTITIES         16
#endif
#endif

#ifndef UR_CFG_MAX_RULES_PER_STATE
#ifdef CONFIG_UR_MAX_RULES_PER_STATE
#define UR_CFG_MAX_RULES_PER_STATE  CONFIG_UR_MAX_RULES_PER_STATE
#else
#define UR_CFG_MAX_RULES_PER_STATE  16
#endif
#endif

#ifndef UR_CFG_MAX_STATES_PER_ENTITY
#ifdef CONFIG_UR_MAX_STATES_PER_ENTITY
#define UR_CFG_MAX_STATES_PER_ENTITY CONFIG_UR_MAX_STATES_PER_ENTITY
#else
#define UR_CFG_MAX_STATES_PER_ENTITY 16
#endif
#endif

#ifndef UR_CFG_MAX_MIXINS_PER_ENTITY
#ifdef CONFIG_UR_MAX_MIXINS_PER_ENTITY
#define UR_CFG_MAX_MIXINS_PER_ENTITY CONFIG_UR_MAX_MIXINS_PER_ENTITY
#else
#define UR_CFG_MAX_MIXINS_PER_ENTITY 4
#endif
#endif

#ifndef UR_CFG_INBOX_SIZE
#ifdef CONFIG_UR_INBOX_SIZE
#define UR_CFG_INBOX_SIZE           CONFIG_UR_INBOX_SIZE
#else
#define UR_CFG_INBOX_SIZE           8
#endif
#endif

#ifndef UR_CFG_SIGNAL_PAYLOAD_SIZE
#ifdef CONFIG_UR_SIGNAL_PAYLOAD_SIZE
#define UR_CFG_SIGNAL_PAYLOAD_SIZE  CONFIG_UR_SIGNAL_PAYLOAD_SIZE
#else
#define UR_CFG_SIGNAL_PAYLOAD_SIZE  4
#endif
#endif

#ifndef UR_CFG_MAX_MIDDLEWARE
#ifdef CONFIG_UR_MAX_MIDDLEWARE
#define UR_CFG_MAX_MIDDLEWARE       CONFIG_UR_MAX_MIDDLEWARE
#else
#define UR_CFG_MAX_MIDDLEWARE       8
#endif
#endif

#ifndef UR_CFG_SCRATCHPAD_SIZE
#ifdef CONFIG_UR_SCRATCHPAD_SIZE
#define UR_CFG_SCRATCHPAD_SIZE      CONFIG_UR_SCRATCHPAD_SIZE
#else
#define UR_CFG_SCRATCHPAD_SIZE      64
#endif
#endif

/* ============================================================================
 * Feature Toggles
 * ========================================================================== */

#ifndef UR_CFG_ENABLE_HSM
#ifdef CONFIG_UR_ENABLE_HSM
#define UR_CFG_ENABLE_HSM           CONFIG_UR_ENABLE_HSM
#else
#define UR_CFG_ENABLE_HSM           1
#endif
#endif

#ifndef UR_CFG_ENABLE_LOGGING
#ifdef CONFIG_UR_ENABLE_LOGGING
#define UR_CFG_ENABLE_LOGGING       CONFIG_UR_ENABLE_LOGGING
#else
#define UR_CFG_ENABLE_LOGGING       0
#endif
#endif

#ifndef UR_CFG_LOG_LEVEL
#ifdef CONFIG_UR_LOG_LEVEL
#define UR_CFG_LOG_LEVEL            CONFIG_UR_LOG_LEVEL
#else
#define UR_CFG_LOG_LEVEL            3
#endif
#endif

#ifndef UR_CFG_ENABLE_TIMESTAMPS
#ifdef CONFIG_UR_ENABLE_TIMESTAMPS
#define UR_CFG_ENABLE_TIMESTAMPS    CONFIG_UR_ENABLE_TIMESTAMPS
#else
#define UR_CFG_ENABLE_TIMESTAMPS    1
#endif
#endif

/* ============================================================================
 * Wormhole Configuration
 * ========================================================================== */

#ifndef UR_CFG_WORMHOLE_ENABLE
#ifdef CONFIG_UR_WORMHOLE_ENABLE
#define UR_CFG_WORMHOLE_ENABLE      CONFIG_UR_WORMHOLE_ENABLE
#else
#define UR_CFG_WORMHOLE_ENABLE      0
#endif
#endif

#ifndef UR_CFG_WORMHOLE_MAX_ROUTES
#ifdef CONFIG_UR_WORMHOLE_MAX_ROUTES
#define UR_CFG_WORMHOLE_MAX_ROUTES  CONFIG_UR_WORMHOLE_MAX_ROUTES
#else
#define UR_CFG_WORMHOLE_MAX_ROUTES  32
#endif
#endif

#ifndef UR_CFG_WORMHOLE_UART_NUM
#ifdef CONFIG_UR_WORMHOLE_UART_NUM
#define UR_CFG_WORMHOLE_UART_NUM    CONFIG_UR_WORMHOLE_UART_NUM
#else
#define UR_CFG_WORMHOLE_UART_NUM    1
#endif
#endif

#ifndef UR_CFG_WORMHOLE_BAUD_RATE
#ifdef CONFIG_UR_WORMHOLE_BAUD_RATE
#define UR_CFG_WORMHOLE_BAUD_RATE   CONFIG_UR_WORMHOLE_BAUD_RATE
#else
#define UR_CFG_WORMHOLE_BAUD_RATE   115200
#endif
#endif

#ifndef UR_CFG_WORMHOLE_RX_TASK_STACK
#ifdef CONFIG_UR_WORMHOLE_RX_TASK_STACK
#define UR_CFG_WORMHOLE_RX_TASK_STACK CONFIG_UR_WORMHOLE_RX_TASK_STACK
#else
#define UR_CFG_WORMHOLE_RX_TASK_STACK 4096
#endif
#endif

#ifndef UR_CFG_WORMHOLE_RX_TASK_PRIORITY
#ifdef CONFIG_UR_WORMHOLE_RX_TASK_PRIORITY
#define UR_CFG_WORMHOLE_RX_TASK_PRIORITY CONFIG_UR_WORMHOLE_RX_TASK_PRIORITY
#else
#define UR_CFG_WORMHOLE_RX_TASK_PRIORITY 5
#endif
#endif

/* ============================================================================
 * Supervisor Configuration
 * ========================================================================== */

#ifndef UR_CFG_SUPERVISOR_ENABLE
#ifdef CONFIG_UR_SUPERVISOR_ENABLE
#define UR_CFG_SUPERVISOR_ENABLE    CONFIG_UR_SUPERVISOR_ENABLE
#else
#define UR_CFG_SUPERVISOR_ENABLE    0
#endif
#endif

#ifndef UR_CFG_SUPERVISOR_MAX_CHILDREN
#ifdef CONFIG_UR_SUPERVISOR_MAX_CHILDREN
#define UR_CFG_SUPERVISOR_MAX_CHILDREN CONFIG_UR_SUPERVISOR_MAX_CHILDREN
#else
#define UR_CFG_SUPERVISOR_MAX_CHILDREN 8
#endif
#endif

#ifndef UR_CFG_SUPERVISOR_RESTART_DELAY_MS
#ifdef CONFIG_UR_SUPERVISOR_RESTART_DELAY_MS
#define UR_CFG_SUPERVISOR_RESTART_DELAY_MS CONFIG_UR_SUPERVISOR_RESTART_DELAY_MS
#else
#define UR_CFG_SUPERVISOR_RESTART_DELAY_MS 100
#endif
#endif

/* ============================================================================
 * Panic Handler Configuration
 * ========================================================================== */

#ifndef UR_CFG_PANIC_ENABLE
#ifdef CONFIG_UR_PANIC_ENABLE
#define UR_CFG_PANIC_ENABLE         CONFIG_UR_PANIC_ENABLE
#else
#define UR_CFG_PANIC_ENABLE         1
#endif
#endif

#ifndef UR_CFG_PANIC_BLACKBOX_SIZE
#ifdef CONFIG_UR_PANIC_BLACKBOX_SIZE
#define UR_CFG_PANIC_BLACKBOX_SIZE  CONFIG_UR_PANIC_BLACKBOX_SIZE
#else
#define UR_CFG_PANIC_BLACKBOX_SIZE  16
#endif
#endif

/* ============================================================================
 * Data Pipe Configuration
 * ========================================================================== */

#ifndef UR_CFG_PIPE_ENABLE
#ifdef CONFIG_UR_PIPE_ENABLE
#define UR_CFG_PIPE_ENABLE          CONFIG_UR_PIPE_ENABLE
#else
#define UR_CFG_PIPE_ENABLE          1
#endif
#endif

#ifndef UR_CFG_PIPE_DEFAULT_SIZE
#ifdef CONFIG_UR_PIPE_DEFAULT_SIZE
#define UR_CFG_PIPE_DEFAULT_SIZE    CONFIG_UR_PIPE_DEFAULT_SIZE
#else
#define UR_CFG_PIPE_DEFAULT_SIZE    256
#endif
#endif

/* ============================================================================
 * Pub/Sub Bus Configuration
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

/* ============================================================================
 * Parameter System Configuration
 * ========================================================================== */

#ifndef UR_CFG_PARAM_ENABLE
#ifdef CONFIG_UR_PARAM_ENABLE
#define UR_CFG_PARAM_ENABLE         CONFIG_UR_PARAM_ENABLE
#else
#define UR_CFG_PARAM_ENABLE         1
#endif
#endif

#ifndef UR_CFG_PARAM_MAX_COUNT
#ifdef CONFIG_UR_PARAM_MAX_COUNT
#define UR_CFG_PARAM_MAX_COUNT      CONFIG_UR_PARAM_MAX_COUNT
#else
#define UR_CFG_PARAM_MAX_COUNT      32
#endif
#endif

#ifndef UR_CFG_PARAM_MAX_STRING_LEN
#ifdef CONFIG_UR_PARAM_MAX_STRING_LEN
#define UR_CFG_PARAM_MAX_STRING_LEN CONFIG_UR_PARAM_MAX_STRING_LEN
#else
#define UR_CFG_PARAM_MAX_STRING_LEN 64
#endif
#endif

/* ============================================================================
 * Signal Codec Configuration
 * ========================================================================== */

#ifndef UR_CFG_CODEC_ENABLE
#ifdef CONFIG_UR_CODEC_ENABLE
#define UR_CFG_CODEC_ENABLE         CONFIG_UR_CODEC_ENABLE
#else
#define UR_CFG_CODEC_ENABLE         1
#endif
#endif

#ifndef UR_CFG_CODEC_MAX_SCHEMAS
#ifdef CONFIG_UR_CODEC_MAX_SCHEMAS
#define UR_CFG_CODEC_MAX_SCHEMAS    CONFIG_UR_CODEC_MAX_SCHEMAS
#else
#define UR_CFG_CODEC_MAX_SCHEMAS    32
#endif
#endif

#ifndef UR_CFG_CODEC_BUFFER_SIZE
#ifdef CONFIG_UR_CODEC_BUFFER_SIZE
#define UR_CFG_CODEC_BUFFER_SIZE    CONFIG_UR_CODEC_BUFFER_SIZE
#else
#define UR_CFG_CODEC_BUFFER_SIZE    256
#endif
#endif

#ifndef UR_CFG_CODEC_JSON_ENABLE
#ifdef CONFIG_UR_CODEC_JSON_ENABLE
#define UR_CFG_CODEC_JSON_ENABLE    CONFIG_UR_CODEC_JSON_ENABLE
#else
#define UR_CFG_CODEC_JSON_ENABLE    1
#endif
#endif

/* ============================================================================
 * Power Management Configuration
 * ========================================================================== */

#ifndef UR_CFG_POWER_ENABLE
#ifdef CONFIG_UR_POWER_ENABLE
#define UR_CFG_POWER_ENABLE         CONFIG_UR_POWER_ENABLE
#else
#define UR_CFG_POWER_ENABLE         1
#endif
#endif

#ifndef UR_CFG_POWER_MAX_MODES
#ifdef CONFIG_UR_POWER_MAX_MODES
#define UR_CFG_POWER_MAX_MODES      CONFIG_UR_POWER_MAX_MODES
#else
#define UR_CFG_POWER_MAX_MODES      4
#endif
#endif

#ifndef UR_CFG_POWER_IDLE_THRESHOLD_MS
#ifdef CONFIG_UR_POWER_IDLE_THRESHOLD_MS
#define UR_CFG_POWER_IDLE_THRESHOLD_MS CONFIG_UR_POWER_IDLE_THRESHOLD_MS
#else
#define UR_CFG_POWER_IDLE_THRESHOLD_MS 100
#endif
#endif

/* ============================================================================
 * Performance Tracing Configuration
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
 * Access Control Configuration
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

/* ============================================================================
 * FreeRTOS Integration
 * ========================================================================== */

#ifndef UR_CFG_USE_FREERTOS
#define UR_CFG_USE_FREERTOS         1
#endif

/* ============================================================================
 * Derived Constants
 * ========================================================================== */

#define UR_SIGNAL_TOTAL_SIZE        (2 + 2 + UR_CFG_SIGNAL_PAYLOAD_SIZE + 4 + 4 + 4)

/* Alignment helpers */
#define UR_ALIGN(x, a)              (((x) + ((a) - 1)) & ~((a) - 1))
#define UR_ALIGN4(x)                UR_ALIGN(x, 4)

#ifdef __cplusplus
}
#endif

#endif /* UR_CONFIG_H */
