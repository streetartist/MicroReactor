/**
 * @file main.c
 * @brief MicroReactor Multi-Entity Example
 *
 * Demonstrates:
 * - Middleware chain (logger, debounce)
 * - Mixin usage (common power rules)
 * - Entity-to-entity communication
 * - Multiple concurrent entities
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "ur_core.h"
#include "ur_flow.h"
#include "ur_utils.h"

static const char *TAG = "multi_entity";

/* ============================================================================
 * Signal Definitions
 * ========================================================================== */

enum {
    /* Sensor signals */
    SIG_TEMP_READING = SIG_USER_BASE,
    SIG_HUMIDITY_READING,

    /* Control signals */
    SIG_FAN_ON,
    SIG_FAN_OFF,
    SIG_ALARM_TRIGGER,
    SIG_ALARM_CLEAR,

    /* Power signals (handled by mixin) */
    SIG_POWER_ON,
    SIG_POWER_OFF,
    SIG_LOW_BATTERY,

    /* Periodic */
    SIG_POLL,
};

/* ============================================================================
 * State Definitions
 * ========================================================================== */

/* Sensor entity states */
enum {
    STATE_SENSOR_IDLE = 1,
    STATE_SENSOR_MEASURING,
    STATE_SENSOR_ERROR,
};

/* Controller entity states */
enum {
    STATE_CTRL_NORMAL = 1,
    STATE_CTRL_COOLING,
    STATE_CTRL_ALARM,
    STATE_CTRL_STANDBY,
};

/* Display entity states */
enum {
    STATE_DISP_OFF = 1,
    STATE_DISP_SHOWING_TEMP,
    STATE_DISP_SHOWING_STATUS,
};

/* ============================================================================
 * Entity Declarations
 * ========================================================================== */

static ur_entity_t sensor_entity;
static ur_entity_t controller_entity;
static ur_entity_t display_entity;

/* ============================================================================
 * Middleware Implementations
 * ========================================================================== */

/* Logger middleware context */
typedef struct {
    uint16_t filter_signal;
    bool log_payload;
} logger_ctx_t;

static logger_ctx_t logger_ctx = {
    .filter_signal = 0,     /* Log all signals */
    .log_payload = true,
};

/* External middleware declaration from ur_transducers.c */
extern ur_mw_result_t ur_mw_logger(ur_entity_t *ent, ur_signal_t *sig, void *ctx);
extern ur_mw_result_t ur_mw_debounce(ur_entity_t *ent, ur_signal_t *sig, void *ctx);

/* Debounce context for alarm signal */
typedef struct {
    uint16_t signal_id;
    uint32_t debounce_ms;
    uint32_t last_time;
} debounce_ctx_t;

static debounce_ctx_t alarm_debounce_ctx = {
    .signal_id = SIG_ALARM_TRIGGER,
    .debounce_ms = 1000,    /* Debounce alarm for 1 second */
    .last_time = 0,
};

/* ============================================================================
 * Mixin: Common Power Rules
 * ========================================================================== */

static uint16_t action_power_off(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)sig;
    ESP_LOGI(TAG, "[%s] Powering off", ur_entity_name(ent));
    return 0;  /* Stay in current state - power off is handled */
}

static uint16_t action_low_battery(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)sig;
    ESP_LOGW(TAG, "[%s] Low battery warning!", ur_entity_name(ent));
    return 0;
}

static const ur_rule_t power_mixin_rules[] = {
    UR_RULE(SIG_POWER_OFF, 0, action_power_off),
    UR_RULE(SIG_LOW_BATTERY, 0, action_low_battery),
    UR_RULE_END
};

static const ur_mixin_t power_mixin = {
    .name = "PowerMixin",
    .rules = power_mixin_rules,
    .rule_count = 2,
    .priority = 10,     /* Higher priority = checked first */
};

/* ============================================================================
 * Sensor Entity
 * ========================================================================== */

typedef struct {
    int32_t temperature;    /* Temperature in 0.1C */
    int32_t humidity;       /* Humidity in 0.1% */
} sensor_scratch_t;

static uint16_t sensor_poll_action(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)sig;
    sensor_scratch_t *s = UR_SCRATCH_PTR(ent, sensor_scratch_t);

    /* Simulate sensor reading */
    s->temperature = 250 + (ur_get_time_ms() % 100);  /* 25.0 - 35.0 C */
    s->humidity = 500 + (ur_get_time_ms() % 200);     /* 50.0 - 70.0 % */

    ESP_LOGI(TAG, "[Sensor] T=%d.%dC H=%d.%d%%",
             s->temperature / 10, s->temperature % 10,
             s->humidity / 10, s->humidity % 10);

    /* Send readings to controller */
    ur_signal_t temp_sig = ur_signal_create_u32(SIG_TEMP_READING, ent->id,
                                                  (uint32_t)s->temperature);
    ur_emit(&controller_entity, temp_sig);

    return STATE_SENSOR_MEASURING;
}

static uint16_t sensor_done_action(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)ent;
    (void)sig;
    return STATE_SENSOR_IDLE;
}

static const ur_rule_t sensor_idle_rules[] = {
    UR_RULE(SIG_POLL, STATE_SENSOR_MEASURING, sensor_poll_action),
    UR_RULE_END
};

static const ur_rule_t sensor_measuring_rules[] = {
    UR_RULE(SIG_SYS_TICK, STATE_SENSOR_IDLE, sensor_done_action),
    UR_RULE(SIG_POLL, 0, sensor_poll_action),  /* Re-poll while measuring */
    UR_RULE_END
};

static const ur_state_def_t sensor_states[] = {
    UR_STATE(STATE_SENSOR_IDLE, 0, NULL, NULL, sensor_idle_rules),
    UR_STATE(STATE_SENSOR_MEASURING, 0, NULL, NULL, sensor_measuring_rules),
};

/* ============================================================================
 * Controller Entity
 * ========================================================================== */

typedef struct {
    int32_t last_temp;
    uint32_t alarm_count;
} controller_scratch_t;

#define TEMP_THRESHOLD_HIGH     300     /* 30.0 C */
#define TEMP_THRESHOLD_CRITICAL 350     /* 35.0 C */

static uint16_t ctrl_temp_received(ur_entity_t *ent, const ur_signal_t *sig)
{
    controller_scratch_t *s = UR_SCRATCH_PTR(ent, controller_scratch_t);
    s->last_temp = (int32_t)sig->payload.u32[0];

    ESP_LOGI(TAG, "[Controller] Received temp: %d.%dC",
             s->last_temp / 10, s->last_temp % 10);

    /* Update display */
    ur_signal_t disp_sig = ur_signal_create_u32(SIG_TEMP_READING, ent->id,
                                                  (uint32_t)s->last_temp);
    ur_emit(&display_entity, disp_sig);

    /* Check thresholds */
    if (s->last_temp >= TEMP_THRESHOLD_CRITICAL) {
        return STATE_CTRL_ALARM;
    } else if (s->last_temp >= TEMP_THRESHOLD_HIGH) {
        return STATE_CTRL_COOLING;
    }

    return 0;  /* Stay in current state */
}

static uint16_t ctrl_enter_cooling(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)sig;
    ESP_LOGI(TAG, "[Controller] Entering COOLING mode");

    /* Send fan on signal to self (could be to a fan entity) */
    ur_signal_t fan_sig = ur_signal_create(SIG_FAN_ON, ent->id);
    ur_emit(ent, fan_sig);

    return 0;
}

static uint16_t ctrl_enter_alarm(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)sig;
    controller_scratch_t *s = UR_SCRATCH_PTR(ent, controller_scratch_t);
    s->alarm_count++;

    ESP_LOGE(TAG, "[Controller] ALARM! Count: %d", s->alarm_count);

    /* Notify display of alarm */
    ur_signal_t alarm_sig = ur_signal_create(SIG_ALARM_TRIGGER, ent->id);
    ur_emit(&display_entity, alarm_sig);

    return 0;
}

static uint16_t ctrl_clear_alarm(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)ent;
    (void)sig;
    ESP_LOGI(TAG, "[Controller] Alarm cleared");

    ur_signal_t clear_sig = ur_signal_create(SIG_ALARM_CLEAR, ent->id);
    ur_emit(&display_entity, clear_sig);

    return STATE_CTRL_NORMAL;
}

static const ur_rule_t ctrl_normal_rules[] = {
    UR_RULE(SIG_TEMP_READING, 0, ctrl_temp_received),
    UR_RULE_END
};

static const ur_rule_t ctrl_cooling_rules[] = {
    UR_RULE(SIG_TEMP_READING, 0, ctrl_temp_received),
    UR_RULE_END
};

static const ur_rule_t ctrl_alarm_rules[] = {
    UR_RULE(SIG_TEMP_READING, 0, ctrl_temp_received),
    UR_RULE(SIG_ALARM_CLEAR, STATE_CTRL_NORMAL, ctrl_clear_alarm),
    UR_RULE_END
};

/* Hierarchical: STANDBY is parent of NORMAL, COOLING, ALARM */
static const ur_rule_t ctrl_standby_rules[] = {
    UR_RULE(SIG_POWER_ON, STATE_CTRL_NORMAL, NULL),
    UR_RULE_END
};

static const ur_state_def_t controller_states[] = {
    UR_STATE(STATE_CTRL_STANDBY, 0, NULL, NULL, ctrl_standby_rules),
    UR_STATE(STATE_CTRL_NORMAL, STATE_CTRL_STANDBY, NULL, NULL, ctrl_normal_rules),
    UR_STATE(STATE_CTRL_COOLING, STATE_CTRL_STANDBY, ctrl_enter_cooling, NULL, ctrl_cooling_rules),
    UR_STATE(STATE_CTRL_ALARM, STATE_CTRL_STANDBY, ctrl_enter_alarm, NULL, ctrl_alarm_rules),
};

/* ============================================================================
 * Display Entity
 * ========================================================================== */

typedef struct {
    int32_t displayed_temp;
    bool alarm_active;
} display_scratch_t;

static uint16_t disp_show_temp(ur_entity_t *ent, const ur_signal_t *sig)
{
    display_scratch_t *s = UR_SCRATCH_PTR(ent, display_scratch_t);
    s->displayed_temp = (int32_t)sig->payload.u32[0];

    ESP_LOGI(TAG, "[Display] Showing: %d.%dC %s",
             s->displayed_temp / 10, s->displayed_temp % 10,
             s->alarm_active ? "[ALARM]" : "");

    return STATE_DISP_SHOWING_TEMP;
}

static uint16_t disp_alarm_on(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)sig;
    display_scratch_t *s = UR_SCRATCH_PTR(ent, display_scratch_t);
    s->alarm_active = true;
    ESP_LOGE(TAG, "[Display] *** ALARM ACTIVE ***");
    return STATE_DISP_SHOWING_STATUS;
}

static uint16_t disp_alarm_off(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)sig;
    display_scratch_t *s = UR_SCRATCH_PTR(ent, display_scratch_t);
    s->alarm_active = false;
    ESP_LOGI(TAG, "[Display] Alarm cleared");
    return STATE_DISP_SHOWING_TEMP;
}

static const ur_rule_t disp_off_rules[] = {
    UR_RULE(SIG_POWER_ON, STATE_DISP_SHOWING_TEMP, NULL),
    UR_RULE_END
};

static const ur_rule_t disp_showing_temp_rules[] = {
    UR_RULE(SIG_TEMP_READING, 0, disp_show_temp),
    UR_RULE(SIG_ALARM_TRIGGER, STATE_DISP_SHOWING_STATUS, disp_alarm_on),
    UR_RULE_END
};

static const ur_rule_t disp_showing_status_rules[] = {
    UR_RULE(SIG_TEMP_READING, 0, disp_show_temp),
    UR_RULE(SIG_ALARM_CLEAR, STATE_DISP_SHOWING_TEMP, disp_alarm_off),
    UR_RULE_END
};

static const ur_state_def_t display_states[] = {
    UR_STATE(STATE_DISP_OFF, 0, NULL, NULL, disp_off_rules),
    UR_STATE(STATE_DISP_SHOWING_TEMP, 0, NULL, NULL, disp_showing_temp_rules),
    UR_STATE(STATE_DISP_SHOWING_STATUS, 0, NULL, NULL, disp_showing_status_rules),
};

/* ============================================================================
 * Tasks
 * ========================================================================== */

static void dispatch_task(void *pvParameters)
{
    ur_entity_t **entities = (ur_entity_t **)pvParameters;

    ESP_LOGI(TAG, "Dispatch task started");

    while (1) {
        /* Round-robin dispatch for all entities */
        ur_dispatch_multi(entities, 3);
        vTaskDelay(1);  /* Yield briefly */
    }
}

static void poll_task(void *pvParameters)
{
    ur_entity_t *sensor = (ur_entity_t *)pvParameters;

    while (1) {
        /* Poll sensor every 2 seconds */
        ur_signal_t poll = ur_signal_create(SIG_POLL, 0);
        ur_emit(sensor, poll);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ============================================================================
 * Main
 * ========================================================================== */

void app_main(void)
{
    ESP_LOGI(TAG, "MicroReactor Multi-Entity Example");
    ESP_LOGI(TAG, "Sensor -> Controller -> Display pipeline with middleware");

    /* Initialize entities */
    ur_entity_config_t sensor_cfg = {
        .id = 1,
        .name = "Sensor",
        .states = sensor_states,
        .state_count = sizeof(sensor_states) / sizeof(sensor_states[0]),
        .initial_state = STATE_SENSOR_IDLE,
    };
    ur_init(&sensor_entity, &sensor_cfg);
    ur_register_entity(&sensor_entity);

    ur_entity_config_t ctrl_cfg = {
        .id = 2,
        .name = "Controller",
        .states = controller_states,
        .state_count = sizeof(controller_states) / sizeof(controller_states[0]),
        .initial_state = STATE_CTRL_NORMAL,
    };
    ur_init(&controller_entity, &ctrl_cfg);
    ur_register_entity(&controller_entity);

    ur_entity_config_t disp_cfg = {
        .id = 3,
        .name = "Display",
        .states = display_states,
        .state_count = sizeof(display_states) / sizeof(display_states[0]),
        .initial_state = STATE_DISP_SHOWING_TEMP,
    };
    ur_init(&display_entity, &disp_cfg);
    ur_register_entity(&display_entity);

    /* Attach power mixin to all entities */
    ur_bind_mixin(&sensor_entity, &power_mixin);
    ur_bind_mixin(&controller_entity, &power_mixin);
    ur_bind_mixin(&display_entity, &power_mixin);

    /* Register middleware on controller (logging and debounce) */
    ur_register_middleware(&controller_entity, ur_mw_logger, &logger_ctx, 0);
    ur_register_middleware(&controller_entity, ur_mw_debounce, &alarm_debounce_ctx, 1);

    /* Start all entities */
    ur_start(&sensor_entity);
    ur_start(&controller_entity);
    ur_start(&display_entity);

    ESP_LOGI(TAG, "All entities started");
    ESP_LOGI(TAG, "  Sensor: state=%d", ur_get_state(&sensor_entity));
    ESP_LOGI(TAG, "  Controller: state=%d", ur_get_state(&controller_entity));
    ESP_LOGI(TAG, "  Display: state=%d", ur_get_state(&display_entity));

    /* Create entity array for dispatch */
    static ur_entity_t *entities[] = {
        &sensor_entity,
        &controller_entity,
        &display_entity,
    };

    /* Create tasks */
    xTaskCreate(dispatch_task, "dispatch", 4096, entities, 5, NULL);
    xTaskCreate(poll_task, "poll", 2048, &sensor_entity, 4, NULL);

    ESP_LOGI(TAG, "System running. Simulating temperature readings...");
}
