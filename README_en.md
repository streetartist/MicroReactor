# MicroReactor v3.0

[![License](https://img.shields.io/badge/license-GPLv3.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20STM32-green.svg)]()
[![Standard](https://img.shields.io/badge/standard-C99-orange.svg)]()

> **A reactive architecture born for microcontrollers. Solid as rock, fluid as flow.**

A zero-allocation reactive embedded framework with Entity-Component-Signal architecture.

---

## Features

- **Zero Dynamic Memory Allocation** - All structures use static allocation
- **Entity-Component-Signal Architecture** - Clean separation of state and behavior
- **FSM + uFlow Hybrid Engine** - State machines with stackless coroutines
- **Middleware Pipeline (Transducers)** - Composable signal processing
- **Data Pipes** - High-throughput streaming with StreamBuffer
- **Pub/Sub Message Bus** - Topic-based message routing, replacing O(N) broadcast
- **Parameter System** - Type-safe KV storage with persistence and change notifications
- **Access Control (ACL)** - Signal firewall with zero-trust architecture
- **Power Management** - Vote-based automatic low-power control
- **Distributed Transparency (Wormhole)** - Cross-chip RPC over UART
- **Self-Healing (Supervisor)** - Automatic entity restart on failure
- **Performance Tracing** - Chrome/Perfetto compatible trace system
- **Signal Codec** - Serialization for MQTT, HTTP, UART bridges

---

## Quick Start

### Add as ESP-IDF Component

Copy the `components/micro_reactor` folder to your project's `components` directory.

### Basic Usage

```c
#include "ur_core.h"
#include "ur_flow.h"

/* Define signals */
enum {
    SIG_BUTTON = SIG_USER_BASE,
    SIG_TIMEOUT,
};

/* Define states */
enum {
    STATE_OFF = 1,
    STATE_ON,
};

/* Define actions */
static uint16_t turn_on(ur_entity_t *ent, const ur_signal_t *sig) {
    gpio_set_level(LED_PIN, 1);
    return 0;  /* Stay in state */
}

static uint16_t turn_off(ur_entity_t *ent, const ur_signal_t *sig) {
    gpio_set_level(LED_PIN, 0);
    return 0;
}

/* Define state rules */
static const ur_rule_t off_rules[] = {
    UR_RULE(SIG_BUTTON, STATE_ON, turn_on),
    UR_RULE_END
};

static const ur_rule_t on_rules[] = {
    UR_RULE(SIG_BUTTON, STATE_OFF, turn_off),
    UR_RULE_END
};

static const ur_state_def_t led_states[] = {
    UR_STATE(STATE_OFF, 0, NULL, NULL, off_rules),
    UR_STATE(STATE_ON, 0, NULL, NULL, on_rules),
};

/* Create and run entity */
static ur_entity_t led;

void app_main(void) {
    ur_entity_config_t cfg = {
        .id = 1,
        .name = "LED",
        .states = led_states,
        .state_count = 2,
        .initial_state = STATE_OFF,
    };

    ur_init(&led, &cfg);
    ur_start(&led);

    /* Recommended: tickless dispatch loop */
    ur_entity_t *entities[] = { &led };
    while (1) {
        ur_run(entities, 1, 100);  /* Idle for 100ms when no signals */
    }
}
```

---

## Architecture

### Entities

Entities are the core reactive units. Each entity has:
- Unique ID
- State machine with states and transition rules
- Inbox queue for signals
- Optional mixins and middleware
- Scratchpad for uFlow coroutine variables

### Signals

Signals are lightweight messages (20 bytes):
- 16-bit signal ID
- 16-bit source entity ID
- 4-byte inline payload (union of u8/u16/u32/i8/i16/i32/float)
- Pointer to external data
- Timestamp

```c
/* Create signals */
ur_signal_t sig = ur_signal_create(SIG_TEMP, entity_id);
sig.payload.u32[0] = temperature;

/* Emit to entity */
ur_emit(&target_entity, sig);

/* Emit from ISR */
ur_emit_from_isr(&target_entity, sig, &woken);
```

### System Signals

Pre-defined system signals (0x0000 - 0x00FF):

| Signal | Value | Description |
|--------|-------|-------------|
| `SIG_NONE` | 0x0000 | Empty signal |
| `SIG_SYS_INIT` | 0x0001 | Entity initialization |
| `SIG_SYS_ENTRY` | 0x0002 | State entry |
| `SIG_SYS_EXIT` | 0x0003 | State exit |
| `SIG_SYS_TICK` | 0x0004 | Periodic tick |
| `SIG_SYS_TIMEOUT` | 0x0005 | Timer timeout |
| `SIG_SYS_DYING` | 0x0006 | Entity dying (supervisor) |
| `SIG_SYS_REVIVE` | 0x0007 | Entity revive request |
| `SIG_SYS_RESET` | 0x0008 | Soft reset |
| `SIG_SYS_SUSPEND` | 0x0009 | Suspend entity |
| `SIG_SYS_RESUME` | 0x000A | Resume entity |
| `SIG_PARAM_CHANGED` | 0x0020 | Parameter changed notification |
| `SIG_PARAM_READY` | 0x0021 | Parameter system ready |
| `SIG_USER_BASE` | 0x0100 | User signal base |

### State Machines

States define behavior through transition rules:

```c
/* Rule: on signal SIG_X, transition to STATE_Y and run action_fn */
UR_RULE(SIG_X, STATE_Y, action_fn)

/* Action signature */
uint16_t action_fn(ur_entity_t *ent, const ur_signal_t *sig) {
    /* Process signal */
    /* Return: 0 = use rule's next_state, non-zero = override state */
    return 0;
}
```

### Hierarchical State Machines (HSM)

States can have parent states for signal bubble-up:

```c
static const ur_state_def_t states[] = {
    UR_STATE(STATE_PARENT, 0, entry_fn, exit_fn, parent_rules),
    UR_STATE(STATE_CHILD, STATE_PARENT, NULL, NULL, child_rules),
};
```

Signal lookup order:
1. Current state rules
2. Mixin rules
3. Parent state rules (HSM bubble-up)

### uFlow Coroutines

Stackless coroutines using Duff's Device:

```c
uint16_t blink_action(ur_entity_t *ent, const ur_signal_t *sig) {
    UR_FLOW_BEGIN(ent);

    while (1) {
        led_on();
        UR_AWAIT_TIME(ent, 500);  /* Wait 500ms */

        led_off();
        UR_AWAIT_SIGNAL(ent, SIG_TICK);  /* Wait for signal */
    }

    UR_FLOW_END(ent);
}
```

Cross-yield variables must use scratchpad:

```c
typedef struct {
    int counter;
    float value;
} my_scratch_t;

UR_SCRATCH_STATIC_ASSERT(my_scratch_t);

/* In action */
my_scratch_t *s = UR_SCRATCH_PTR(ent, my_scratch_t);
s->counter++;
```

### Middleware (Transducers)

Middleware processes signals before state rules:

```c
/* Middleware function */
ur_mw_result_t my_middleware(ur_entity_t *ent, ur_signal_t *sig, void *ctx) {
    if (should_filter(sig)) {
        return UR_MW_FILTERED;  /* Drop signal */
    }
    return UR_MW_CONTINUE;  /* Pass to next */
}

/* Register */
ur_register_middleware(&entity, my_middleware, context, priority);
```

Middleware return values:
- `UR_MW_CONTINUE` - Continue to next middleware
- `UR_MW_HANDLED` - Signal handled, stop processing
- `UR_MW_FILTERED` - Filter and drop signal
- `UR_MW_TRANSFORM` - Signal modified, continue processing

### Mixins

State-agnostic signal handlers:

```c
static const ur_rule_t power_rules[] = {
    UR_RULE(SIG_POWER_OFF, 0, handle_power_off),
    UR_RULE_END
};

static const ur_mixin_t power_mixin = {
    .name = "Power",
    .rules = power_rules,
    .rule_count = 1,
    .priority = 10,
};

ur_bind_mixin(&entity, &power_mixin);
```

### Data Pipes

High-throughput streaming:

```c
/* Static buffer */
static uint8_t buffer[1024];
static ur_pipe_t pipe;

/* Initialize */
ur_pipe_init(&pipe, buffer, sizeof(buffer), 64);

/* Write (task context) */
ur_pipe_write(&pipe, data, size, timeout_ms);

/* Write (ISR context) */
ur_pipe_write_from_isr(&pipe, data, size, &woken);

/* Read */
size_t read = ur_pipe_read(&pipe, buffer, size, timeout_ms);

/* Status */
size_t available = ur_pipe_available(&pipe);
size_t space = ur_pipe_space(&pipe);
```

### Pub/Sub Message Bus

Topic-based message distribution:

```c
#include "ur_bus.h"

/* Initialize bus */
ur_bus_init();

/* Subscribe to topic */
ur_subscribe(&sensor_entity, TOPIC_TEMPERATURE);
ur_subscribe(&display_entity, TOPIC_TEMPERATURE);

/* Publish (all subscribers receive) */
ur_publish(ur_signal_create(TOPIC_TEMPERATURE, src_id));

/* Publish with payload */
ur_publish_u32(TOPIC_TEMPERATURE, src_id, temperature_value);
```

### Parameter System

Type-safe KV storage:

```c
#include "ur_param.h"

/* Define parameters */
enum {
    PARAM_BRIGHTNESS = 1,
    PARAM_VOLUME,
    PARAM_DEVICE_NAME,
};

static const ur_param_def_t params[] = {
    { PARAM_BRIGHTNESS, UR_PARAM_TYPE_U8, "brightness", .default_val.u8 = 50, UR_PARAM_FLAG_PERSIST },
    { PARAM_VOLUME, UR_PARAM_TYPE_U8, "volume", .default_val.u8 = 80, UR_PARAM_FLAG_PERSIST | UR_PARAM_FLAG_NOTIFY },
    { PARAM_DEVICE_NAME, UR_PARAM_TYPE_STR, "name", .default_val.str = "Device", UR_PARAM_FLAG_PERSIST },
};

/* Initialize */
ur_param_init(params, 3, &storage_backend);

/* Read/write parameters */
uint8_t brightness;
ur_param_get_u8(PARAM_BRIGHTNESS, &brightness);
ur_param_set_u8(PARAM_BRIGHTNESS, 75);

/* Persist */
ur_param_save_all();
```

### Access Control List (ACL)

Signal firewall:

```c
#include "ur_acl.h"

/* Define ACL rules */
static const ur_acl_rule_t sensor_rules[] = {
    UR_ACL_ALLOW_FROM(ENT_CONTROLLER),      /* Allow signals from controller */
    UR_ACL_DENY_FROM(UR_ACL_SRC_EXTERNAL),  /* Deny external signals */
    UR_ACL_ALLOW_SIG(SIG_SYS_TICK),         /* Allow system tick */
};

/* Register rules */
ur_acl_register(&sensor_entity, sensor_rules, 3);

/* Set default policy */
ur_acl_set_default(&sensor_entity, UR_ACL_DEFAULT_DENY);

/* Enable ACL middleware */
ur_acl_enable_middleware(&sensor_entity);
```

### Power Management

Vote-based low-power control:

```c
#include "ur_power.h"

/* Initialize */
ur_power_init(&ur_power_hal_esp);

/* Acquire power lock (prevent low-power) */
ur_power_lock(&wifi_entity, UR_POWER_ACTIVE);

/* Release power lock */
ur_power_unlock(&wifi_entity, UR_POWER_ACTIVE);

/* Enter low-power (framework selects allowed mode) */
ur_power_mode_t allowed = ur_power_get_allowed_mode();
ur_power_enter_mode(allowed, timeout_ms, wake_sources);
```

### Wormhole (Cross-chip RPC)

Distributed signal routing over UART:

```c
#include "ur_wormhole.h"

/* Initialize */
ur_wormhole_init(chip_id);

/* Add route: local entity 1 <-> remote entity 100 */
ur_wormhole_add_route(1, 100, UART_NUM_1);

/* Send to remote */
ur_wormhole_send(100, signal);
```

Protocol frame (10 bytes):
```
| 0xAA | SrcID (2B) | SigID (2B) | Payload (4B) | CRC8 |
```

### Supervisor (Self-Healing)

Automatic entity restart:

```c
#include "ur_supervisor.h"

/* Create supervisor */
ur_supervisor_create(&supervisor_entity, max_restarts);

/* Add child entities */
ur_supervisor_add_child(&supervisor_entity, &child1);
ur_supervisor_add_child(&supervisor_entity, &child2);

/* Report failure (triggers restart) */
ur_report_dying(&child1, error_code);
```

### Performance Tracing

Chrome/Perfetto compatible trace system:

```c
#include "ur_trace.h"

/* Initialize */
ur_trace_init();
ur_trace_set_backend(&ur_trace_backend_uart);
ur_trace_enable(true);

/* Register metadata */
ur_trace_register_entity_name(ENT_LED, "LED");
ur_trace_register_signal_name(SIG_BUTTON, "BUTTON");
ur_trace_register_state_name(ENT_LED, STATE_ON, "On");
ur_trace_sync_metadata();

/* Trace macros (zero overhead when disabled) */
UR_TRACE_START(ent_id, sig_id);
UR_TRACE_END(ent_id, sig_id);
UR_TRACE_TRANSITION(ent_id, from_state, to_state);
```

### Signal Codec

Serialization for MQTT, HTTP, UART bridges:

```c
#include "ur_codec.h"

/* Encode to binary */
uint8_t buffer[256];
size_t len;
ur_codec_encode_binary(&signal, buffer, sizeof(buffer), &len);

/* Encode to JSON */
char json[256];
ur_codec_encode_json(&signal, json, sizeof(json), &len);

/* Decode */
ur_signal_t decoded;
ur_codec_decode_json(json, &decoded);
```

---

## Configuration

Configure via `menuconfig` or `sdkconfig`:

### Core Configuration

```
CONFIG_UR_MAX_ENTITIES=16           # Maximum entities
CONFIG_UR_MAX_RULES_PER_STATE=16    # Max rules per state
CONFIG_UR_MAX_STATES_PER_ENTITY=16  # Max states per entity
CONFIG_UR_MAX_MIXINS_PER_ENTITY=4   # Max mixins per entity
CONFIG_UR_INBOX_SIZE=8              # Inbox queue size
CONFIG_UR_SCRATCHPAD_SIZE=64        # Coroutine scratchpad size
CONFIG_UR_MAX_MIDDLEWARE=8          # Max middleware count
CONFIG_UR_SIGNAL_PAYLOAD_SIZE=4     # Signal inline payload size
```

### Feature Switches

```
CONFIG_UR_ENABLE_HSM=y              # Enable hierarchical state machine
CONFIG_UR_ENABLE_LOGGING=n          # Enable debug logging
CONFIG_UR_ENABLE_TIMESTAMPS=y       # Enable timestamps
CONFIG_UR_PIPE_ENABLE=y             # Enable data pipes
CONFIG_UR_BUS_ENABLE=y              # Enable Pub/Sub bus
CONFIG_UR_PARAM_ENABLE=y            # Enable parameter system
CONFIG_UR_CODEC_ENABLE=y            # Enable codec
CONFIG_UR_POWER_ENABLE=y            # Enable power management
CONFIG_UR_TRACE_ENABLE=n            # Enable performance tracing
CONFIG_UR_ACL_ENABLE=y              # Enable access control
CONFIG_UR_WORMHOLE_ENABLE=n         # Enable wormhole
CONFIG_UR_SUPERVISOR_ENABLE=n       # Enable supervisor
```

### Extension Module Configuration

```
CONFIG_UR_BUS_MAX_TOPICS=64         # Max topics
CONFIG_UR_BUS_MAX_SUBSCRIBERS=8     # Max subscribers per topic
CONFIG_UR_PARAM_MAX_COUNT=32        # Max parameters
CONFIG_UR_PARAM_MAX_STRING_LEN=64   # Max parameter string length
CONFIG_UR_CODEC_MAX_SCHEMAS=32      # Max codec schemas
CONFIG_UR_CODEC_BUFFER_SIZE=256     # Codec buffer size
CONFIG_UR_ACL_MAX_RULES=32          # Max ACL rules
CONFIG_UR_TRACE_BUFFER_SIZE=4096    # Trace buffer size
CONFIG_UR_TRACE_MAX_ENTRIES=256     # Max trace entries
```

---

## Examples

### example_basic

LED blinker with button control:
- Entity initialization
- Signal emission
- FSM state transitions
- uFlow coroutine timing
- GPIO ISR handling

### example_multi_entity

Temperature monitoring system:
- Multi-entity coordination
- Middleware chain (logger, debounce)
- Mixin usage (common power rules)
- HSM hierarchical states
- Entity-to-entity communication

### example_pipe

Audio-style data streaming:
- Producer-consumer pattern
- ISR-safe writes
- Throughput monitoring

---

## API Reference

### Core Functions

```c
/* Entity management */
ur_err_t ur_init(ur_entity_t *ent, const ur_entity_config_t *config);
ur_err_t ur_start(ur_entity_t *ent);
ur_err_t ur_stop(ur_entity_t *ent);
ur_err_t ur_suspend(ur_entity_t *ent);
ur_err_t ur_resume(ur_entity_t *ent);

/* Signal emission */
ur_err_t ur_emit(ur_entity_t *target, ur_signal_t sig);
ur_err_t ur_emit_from_isr(ur_entity_t *target, ur_signal_t sig, BaseType_t *woken);
ur_err_t ur_emit_to_id(uint16_t target_id, ur_signal_t sig);
int ur_broadcast(ur_signal_t sig);

/* Dispatch loop */
ur_err_t ur_dispatch(ur_entity_t *ent, uint32_t timeout_ms);
int ur_dispatch_all(ur_entity_t *ent);
int ur_dispatch_multi(ur_entity_t **entities, size_t count);
int ur_run(ur_entity_t **entities, size_t count, uint32_t idle_ms);
```

### State Management

```c
uint16_t ur_get_state(const ur_entity_t *ent);
ur_err_t ur_set_state(ur_entity_t *ent, uint16_t state_id);
bool ur_in_state(const ur_entity_t *ent, uint16_t state_id);
```

### Mixin/Middleware

```c
ur_err_t ur_bind_mixin(ur_entity_t *ent, const ur_mixin_t *mixin);
ur_err_t ur_unbind_mixin(ur_entity_t *ent, const ur_mixin_t *mixin);
ur_err_t ur_register_middleware(ur_entity_t *ent, ur_middleware_fn_t fn, void *ctx, uint8_t priority);
ur_err_t ur_unregister_middleware(ur_entity_t *ent, ur_middleware_fn_t fn);
ur_err_t ur_set_middleware_enabled(ur_entity_t *ent, ur_middleware_fn_t fn, bool enabled);
```

### Entity Registry

```c
ur_err_t ur_register_entity(ur_entity_t *ent);
ur_err_t ur_unregister_entity(ur_entity_t *ent);
ur_entity_t *ur_get_entity(uint16_t id);
size_t ur_get_entity_count(void);
```

### Pipe Functions

```c
ur_err_t ur_pipe_init(ur_pipe_t *pipe, uint8_t *buffer, size_t size, size_t trigger);
ur_err_t ur_pipe_reset(ur_pipe_t *pipe);
size_t ur_pipe_write(ur_pipe_t *pipe, const void *data, size_t size, uint32_t timeout_ms);
size_t ur_pipe_write_from_isr(ur_pipe_t *pipe, const void *data, size_t size, BaseType_t *woken);
size_t ur_pipe_read(ur_pipe_t *pipe, void *buffer, size_t size, uint32_t timeout_ms);
size_t ur_pipe_read_from_isr(ur_pipe_t *pipe, void *buffer, size_t size, BaseType_t *woken);
size_t ur_pipe_available(const ur_pipe_t *pipe);
size_t ur_pipe_space(const ur_pipe_t *pipe);
bool ur_pipe_is_empty(const ur_pipe_t *pipe);
bool ur_pipe_is_full(const ur_pipe_t *pipe);
```

### Utility Functions

```c
bool ur_in_isr(void);
uint32_t ur_get_time_ms(void);
size_t ur_inbox_count(const ur_entity_t *ent);
bool ur_inbox_empty(const ur_entity_t *ent);
void ur_inbox_clear(ur_entity_t *ent);
```

---

## Error Codes

| Code | Description |
|------|-------------|
| `UR_OK` | Success |
| `UR_ERR_INVALID_ARG` | Invalid argument |
| `UR_ERR_NO_MEMORY` | Static pool exhausted |
| `UR_ERR_QUEUE_FULL` | Inbox full |
| `UR_ERR_NOT_FOUND` | Entity/state not found |
| `UR_ERR_INVALID_STATE` | Invalid state transition |
| `UR_ERR_TIMEOUT` | Operation timed out |
| `UR_ERR_ALREADY_EXISTS` | Item already exists |
| `UR_ERR_DISABLED` | Feature disabled |

---

## Target Platform

- **ESP-IDF 5.x** (FreeRTOS v10.5+)
- Modern FreeRTOS API:
  - `xPortInIsrContext()` for ISR detection
  - `xStreamBufferCreateStatic()` for pipes
  - `xQueueCreateStatic()` for inboxes

---

## License

GPL v3 License

---

## Documentation

- [Tutorial (Chinese)](docs/tutorial_zh.md)
- [API Reference (Chinese)](docs/api_reference_zh.md)

---

## Development Tools

MicroReactor provides a complete set of visual development tools in the `tools/` directory.

### Install Dependencies

```bash
cd tools
pip install -r requirements.txt
```

### Reactor Studio - Visual State Machine Designer

```bash
python tools/studio/reactor_studio.py
```

Drag-and-drop state machine editor that generates MicroReactor compatible C code.

**Features:**
- Visual editing: Drag to create states, visual transition lines
- HSM support: Parent-child state relationships
- State editor: Configure entry/exit actions, transition rules
- Multi-entity projects: Manage multiple state machines
- Code generation: Auto-generate .h and .c files
- Project management: JSON format save/load
- Bilingual UI: Chinese/English toggle

**Shortcuts**: `S` Add state | `T` Transition mode | `Delete` Remove | `Ctrl+S` Save | `Ctrl+E` Export

---

### Reactor Scope - Real-time Monitor

```bash
python tools/scope/reactor_scope.py
```

Connect to device serial port for real-time MicroReactor system monitoring.

**Features:**
- Gantt timeline: Visualize dispatch timeline per entity
- Signal flow diagram: Show signal sequence between entities
- Performance stats: Signal rate, dispatch timing, memory usage
- Metadata display: Entity/signal/state names (synced from device)
- Signal injection: Manually send signals for testing
- Command terminal: Send shell commands
- Data export: JSON/CSV format

**Enable Device Tracing:**
```c
UR_TRACE_INIT();
ur_trace_set_backend(&ur_trace_backend_uart);
ur_trace_enable(true);

// Register metadata (optional, for readable names in Scope)
ur_trace_register_entity_name(ENT_ID_SYSTEM, "System");
ur_trace_register_signal_name(SIG_BUTTON, "BUTTON");
ur_trace_register_state_name(ENT_ID_SYSTEM, STATE_IDLE, "Idle");
ur_trace_sync_metadata();
```

---

### Reactor CTL - Command Line Tool

```bash
python tools/rctl.py -p COM3 list          # List entities
python tools/rctl.py -p COM3 inject 1 0x100  # Inject signal
python tools/rctl.py -p COM3 listen         # Listen for signals
```

---

### Crash Analyzer - Black Box Decoder

```bash
python tools/crash_analyzer.py dump.bin --elf firmware.elf
```

Analyze MicroReactor black box dump data, generate event timeline and diagnostic report.

---

Full tool documentation: [tools/README.md](tools/README.md)
