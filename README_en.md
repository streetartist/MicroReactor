# MicroReactor (FluxOne) v3.0

A zero-allocation reactive embedded framework with Entity-Component-Signal architecture.

## Features

- **Zero Dynamic Memory Allocation** - All structures use static allocation
- **Entity-Component-Signal Architecture** - Clean separation of state and behavior
- **FSM + uFlow Hybrid Engine** - State machines with stackless coroutines
- **Middleware Pipeline (Transducers)** - Composable signal processing
- **Data Pipes** - High-throughput streaming with StreamBuffer
- **Distributed Transparency (Wormhole)** - Cross-chip RPC over UART
- **Self-Healing (Supervisor)** - Automatic entity restart on failure

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

    while (1) {
        ur_dispatch(&led, portMAX_DELAY);
    }
}
```

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
- 4-byte inline payload (union of u8/u16/u32/float)
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

Built-in middleware:
- `ur_mw_logger` - Signal logging
- `ur_mw_debounce` - Debounce filter
- `ur_mw_throttle` - Rate limiting
- `ur_mw_filter` - Custom predicate filter

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

### Wormhole (Cross-chip RPC)

Distributed signal routing over UART:

```c
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
/* Create supervisor */
ur_supervisor_create(&supervisor_entity, max_restarts);

/* Add child entities */
ur_supervisor_add_child(&supervisor_entity, &child1);
ur_supervisor_add_child(&supervisor_entity, &child2);

/* Report failure (triggers restart) */
ur_report_dying(&child1, error_code);
```

## Configuration

Configure via `menuconfig` or `sdkconfig`:

```
CONFIG_UR_MAX_ENTITIES=16
CONFIG_UR_MAX_RULES_PER_STATE=16
CONFIG_UR_MAX_STATES_PER_ENTITY=16
CONFIG_UR_MAX_MIXINS_PER_ENTITY=4
CONFIG_UR_INBOX_SIZE=8
CONFIG_UR_SCRATCHPAD_SIZE=64
CONFIG_UR_MAX_MIDDLEWARE=8
CONFIG_UR_ENABLE_HSM=y
CONFIG_UR_ENABLE_LOGGING=n
CONFIG_UR_ENABLE_TIMESTAMPS=y
```

## Examples

### example_basic

LED blinker with button control:
- Entity initialization
- Signal emission
- FSM state transitions
- uFlow coroutine timing

### example_multi_entity

Temperature monitoring system:
- Middleware chain (logger, debounce)
- Mixin usage (common power rules)
- Entity-to-entity communication

### example_pipe

Audio-style data streaming:
- Producer-consumer pattern
- ISR-safe writes
- Throughput monitoring

## API Reference

### Core Functions

```c
ur_err_t ur_init(ur_entity_t *ent, const ur_entity_config_t *config);
ur_err_t ur_start(ur_entity_t *ent);
ur_err_t ur_stop(ur_entity_t *ent);
ur_err_t ur_emit(ur_entity_t *target, ur_signal_t sig);
ur_err_t ur_emit_from_isr(ur_entity_t *target, ur_signal_t sig, BaseType_t *woken);
ur_err_t ur_dispatch(ur_entity_t *ent, uint32_t timeout_ms);
int ur_dispatch_all(ur_entity_t *ent);
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
ur_err_t ur_register_middleware(ur_entity_t *ent, ur_middleware_fn_t fn, void *ctx, uint8_t priority);
```

### Pipe Functions

```c
ur_err_t ur_pipe_init(ur_pipe_t *pipe, uint8_t *buffer, size_t size, size_t trigger);
size_t ur_pipe_write(ur_pipe_t *pipe, const void *data, size_t size, uint32_t timeout_ms);
size_t ur_pipe_read(ur_pipe_t *pipe, void *buffer, size_t size, uint32_t timeout_ms);
size_t ur_pipe_available(const ur_pipe_t *pipe);
```

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

## Target Platform

- **ESP-IDF 5.x** (FreeRTOS v10.5+)
- Modern FreeRTOS API:
  - `xPortInIsrContext()` for ISR detection
  - `xStreamBufferCreateStatic()` for pipes
  - `xQueueCreateStatic()` for inboxes

## License

MIT License

## Contributing

Contributions welcome! Please read the architecture document before submitting PRs.
