/**
 * @file main.c
 * @brief MicroReactor Advanced Features Example
 *
 * Demonstrates the new features:
 * - Pub/Sub Bus (ur_bus)
 * - Persistent Parameters (ur_param)
 * - Signal Codec (ur_codec)
 * - Power Management (ur_power)
 * - Access Control (ur_acl)
 *
 * This example simulates a smart speaker with:
 * - Battery monitoring entity
 * - UI entity (subscribes to battery/wifi status)
 * - Audio entity (prevents sleep while playing)
 * - RPC gateway (external control)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ur_core.h"
#include "ur_bus.h"
#include "ur_param.h"
#include "ur_codec.h"
#include "ur_power.h"
#include "ur_acl.h"
#include "ur_trace.h"

/* ============================================================================
 * Signal Definitions
 * ========================================================================== */

enum {
    /* Battery signals */
    SIG_BATTERY_LEVEL = 0x0100,
    SIG_BATTERY_LOW,
    SIG_BATTERY_CRITICAL,

    /* WiFi signals */
    SIG_WIFI_CONNECTED = 0x0110,
    SIG_WIFI_DISCONNECTED,
    SIG_WIFI_STATUS,

    /* Audio signals */
    SIG_AUDIO_PLAY = 0x0120,
    SIG_AUDIO_PAUSE,
    SIG_AUDIO_STOP,
    SIG_AUDIO_VOLUME,

    /* System signals */
    SIG_FACTORY_RESET = 0x0130,
    SIG_SHUTDOWN,
};

/* ============================================================================
 * Parameter Definitions
 * ========================================================================== */

enum {
    PARAM_VOLUME = 1,
    PARAM_BRIGHTNESS,
    PARAM_WIFI_SSID,
    PARAM_WIFI_PASS,
    PARAM_AUTO_SLEEP,
};

static const ur_param_def_t g_params[] = {
    {
        .id = PARAM_VOLUME,
        .type = UR_PARAM_TYPE_U8,
        .flags = UR_PARAM_FLAG_PERSIST | UR_PARAM_FLAG_NOTIFY,
        .name = "volume",
        .default_val = { .u8 = 50 }
    },
    {
        .id = PARAM_BRIGHTNESS,
        .type = UR_PARAM_TYPE_U8,
        .flags = UR_PARAM_FLAG_PERSIST | UR_PARAM_FLAG_NOTIFY,
        .name = "brightness",
        .default_val = { .u8 = 100 }
    },
    {
        .id = PARAM_WIFI_SSID,
        .type = UR_PARAM_TYPE_STR,
        .flags = UR_PARAM_FLAG_PERSIST,
        .name = "wifi_ssid",
        .size = 32,
        .default_val = { .str = "" }
    },
    {
        .id = PARAM_AUTO_SLEEP,
        .type = UR_PARAM_TYPE_BOOL,
        .flags = UR_PARAM_FLAG_PERSIST | UR_PARAM_FLAG_NOTIFY,
        .name = "auto_sleep",
        .default_val = { .b = true }
    },
};

/* ============================================================================
 * Codec Schema Definitions
 * ========================================================================== */

static const ur_codec_field_t audio_play_fields[] = {
    { "volume", UR_FIELD_U8, 0, 0 },
    { "track_id", UR_FIELD_U16, 1, 0 },
};

static const ur_codec_schema_t audio_play_schema = {
    .signal_id = SIG_AUDIO_PLAY,
    .name = "audio_play",
    .fields = audio_play_fields,
    .field_count = 2,
    .payload_size = 3,
};

/* ============================================================================
 * Entity Definitions
 * ========================================================================== */

/* Entity IDs */
enum {
    ID_BATTERY = 1,
    ID_UI,
    ID_AUDIO,
    ID_RPC_GATEWAY,
};

/* States */
enum {
    STATE_IDLE = 1,
    STATE_ACTIVE,
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_LOW_POWER,
};

/* Entity instances */
static ur_entity_t g_battery_ent;
static ur_entity_t g_ui_ent;
static ur_entity_t g_audio_ent;
static ur_entity_t g_rpc_ent;

/* ============================================================================
 * Battery Entity
 * ========================================================================== */

static uint8_t g_battery_level = 100;

static uint16_t battery_tick(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)sig;

    /* Simulate battery drain */
    if (g_battery_level > 0) {
        g_battery_level--;
    }

    /* Publish battery level (only subscribers receive it) */
    ur_publish_u32(SIG_BATTERY_LEVEL, ent->id, g_battery_level);

    if (g_battery_level == 20) {
        ur_publish_u32(SIG_BATTERY_LOW, ent->id, g_battery_level);
    }

    if (g_battery_level == 5) {
        ur_publish_u32(SIG_BATTERY_CRITICAL, ent->id, g_battery_level);
    }

    return 0;
}

static const ur_rule_t battery_rules[] = {
    UR_RULE(SIG_SYS_TICK, 0, battery_tick),
    UR_RULE_END
};

static const ur_state_def_t battery_states[] = {
    UR_STATE(STATE_ACTIVE, 0, NULL, NULL, battery_rules),
};

/* ============================================================================
 * UI Entity (subscribes to battery/wifi)
 * ========================================================================== */

static uint16_t ui_on_battery(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)ent;
    uint8_t level = sig->payload.u8[0];
    printf("[UI] Battery level: %d%%\n", level);
    return 0;
}

static uint16_t ui_on_battery_low(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)ent;
    (void)sig;
    printf("[UI] WARNING: Battery low!\n");
    return 0;
}

static uint16_t ui_on_param_changed(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)ent;
    uint16_t param_id = sig->payload.u16[0];

    if (param_id == PARAM_VOLUME) {
        uint8_t vol;
        ur_param_get_u8(PARAM_VOLUME, &vol);
        printf("[UI] Volume changed to: %d\n", vol);
    }

    return 0;
}

static const ur_rule_t ui_rules[] = {
    UR_RULE(SIG_BATTERY_LEVEL, 0, ui_on_battery),
    UR_RULE(SIG_BATTERY_LOW, 0, ui_on_battery_low),
    UR_RULE(SIG_PARAM_CHANGED, 0, ui_on_param_changed),
    UR_RULE_END
};

static const ur_state_def_t ui_states[] = {
    UR_STATE(STATE_ACTIVE, 0, NULL, NULL, ui_rules),
};

/* ============================================================================
 * Audio Entity (uses power locks)
 * ========================================================================== */

static uint16_t audio_on_play(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)sig;

    /* Lock sleep while playing */
    ur_power_lock(ent, UR_POWER_LIGHT_SLEEP);

    printf("[Audio] Playing... (sleep locked)\n");

    return STATE_PLAYING;
}

static uint16_t audio_on_stop(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)sig;

    /* Release sleep lock */
    ur_power_unlock(ent, UR_POWER_LIGHT_SLEEP);

    printf("[Audio] Stopped (sleep unlocked)\n");

    return STATE_IDLE;
}

static uint16_t audio_on_volume(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)ent;
    uint8_t vol = sig->payload.u8[0];

    /* Update parameter (auto-persists and notifies) */
    ur_param_set_u8(PARAM_VOLUME, vol);

    printf("[Audio] Volume set to %d\n", vol);

    return 0;
}

static const ur_rule_t audio_idle_rules[] = {
    UR_RULE(SIG_AUDIO_PLAY, STATE_PLAYING, audio_on_play),
    UR_RULE(SIG_AUDIO_VOLUME, 0, audio_on_volume),
    UR_RULE_END
};

static const ur_rule_t audio_playing_rules[] = {
    UR_RULE(SIG_AUDIO_STOP, STATE_IDLE, audio_on_stop),
    UR_RULE(SIG_AUDIO_PAUSE, STATE_PAUSED, NULL),
    UR_RULE(SIG_AUDIO_VOLUME, 0, audio_on_volume),
    UR_RULE_END
};

static const ur_state_def_t audio_states[] = {
    UR_STATE(STATE_IDLE, 0, NULL, NULL, audio_idle_rules),
    UR_STATE(STATE_PLAYING, 0, NULL, NULL, audio_playing_rules),
};

/* ============================================================================
 * RPC Gateway Entity (receives external signals)
 * ========================================================================== */

static void rpc_on_receive(const ur_signal_t *sig, void *source)
{
    (void)source;

    printf("[RPC] Received signal 0x%04X from external source\n", sig->id);

    /* Forward to appropriate entity (ACL will filter) */
    if (sig->id >= SIG_AUDIO_PLAY && sig->id <= SIG_AUDIO_VOLUME) {
        ur_emit(&g_audio_ent, *sig);
    }
}

static const ur_rule_t rpc_rules[] = {
    UR_RULE_END
};

static const ur_state_def_t rpc_states[] = {
    UR_STATE(STATE_ACTIVE, 0, NULL, NULL, rpc_rules),
};

/* ============================================================================
 * ACL Configuration (security)
 * ========================================================================== */

/* Protect Audio entity from dangerous signals from external sources */
static const ur_acl_rule_t audio_acl_rules[] = {
    /* Allow all signals from local entities */
    UR_ACL_ALLOW_FROM(UR_ACL_SRC_LOCAL),

    /* Allow play/pause/stop from external */
    { UR_ACL_SRC_EXTERNAL, SIG_AUDIO_PLAY, UR_ACL_ALLOW, 0, UR_ACL_FLAG_LOG },
    { UR_ACL_SRC_EXTERNAL, SIG_AUDIO_PAUSE, UR_ACL_ALLOW, 0, 0 },
    { UR_ACL_SRC_EXTERNAL, SIG_AUDIO_STOP, UR_ACL_ALLOW, 0, 0 },
    { UR_ACL_SRC_EXTERNAL, SIG_AUDIO_VOLUME, UR_ACL_ALLOW, 0, 0 },

    /* Block factory reset from external */
    { UR_ACL_SRC_EXTERNAL, SIG_FACTORY_RESET, UR_ACL_DENY, 0, UR_ACL_FLAG_LOG },
    { UR_ACL_SRC_EXTERNAL, SIG_SHUTDOWN, UR_ACL_DENY, 0, UR_ACL_FLAG_LOG },
};

/* ============================================================================
 * Main Application
 * ========================================================================== */

static void dispatch_task(void *arg)
{
    ur_entity_t **entities = (ur_entity_t **)arg;

    while (1) {
        /* Dispatch all entities */
        int processed = ur_dispatch_multi(entities, 4);

        if (processed == 0) {
            /* No signals - try to sleep */
            uint32_t next_event = ur_power_get_next_event_ms();
            if (next_event > 100) {
                ur_power_idle(next_event);
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
    }
}

static void battery_sim_task(void *arg)
{
    (void)arg;

    while (1) {
        /* Generate tick signal every second */
        ur_signal_t tick = { .id = SIG_SYS_TICK, .src_id = 0 };
        ur_emit(&g_battery_ent, tick);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    printf("MicroReactor Advanced Features Example\n");
    printf("======================================\n\n");

    /* Initialize subsystems */
    ur_bus_init();
    ur_param_init(g_params, sizeof(g_params) / sizeof(g_params[0]),
                  &ur_param_storage_nvs);
    ur_codec_init();
    ur_power_init(&ur_power_hal_esp);
    ur_acl_init();

    UR_TRACE_INIT();

    /* Register codec schema */
    ur_codec_register_schema(&audio_play_schema);

    /* Set up RPC callback */
    ur_rpc_set_recv_callback(rpc_on_receive);

    /* Initialize entities */
    ur_entity_config_t battery_cfg = {
        .id = ID_BATTERY,
        .name = "Battery",
        .states = battery_states,
        .state_count = 1,
        .initial_state = STATE_ACTIVE,
    };
    ur_init(&g_battery_ent, &battery_cfg);
    ur_register_entity(&g_battery_ent);

    ur_entity_config_t ui_cfg = {
        .id = ID_UI,
        .name = "UI",
        .states = ui_states,
        .state_count = 1,
        .initial_state = STATE_ACTIVE,
    };
    ur_init(&g_ui_ent, &ui_cfg);
    ur_register_entity(&g_ui_ent);

    ur_entity_config_t audio_cfg = {
        .id = ID_AUDIO,
        .name = "Audio",
        .states = audio_states,
        .state_count = 2,
        .initial_state = STATE_IDLE,
    };
    ur_init(&g_audio_ent, &audio_cfg);
    ur_register_entity(&g_audio_ent);

    ur_entity_config_t rpc_cfg = {
        .id = ID_RPC_GATEWAY,
        .name = "RPC",
        .states = rpc_states,
        .state_count = 1,
        .initial_state = STATE_ACTIVE,
    };
    ur_init(&g_rpc_ent, &rpc_cfg);
    ur_register_entity(&g_rpc_ent);

    /* Set up subscriptions (UI subscribes to battery/wifi/params) */
    ur_subscribe(&g_ui_ent, SIG_BATTERY_LEVEL);
    ur_subscribe(&g_ui_ent, SIG_BATTERY_LOW);
    ur_subscribe(&g_ui_ent, SIG_BATTERY_CRITICAL);
    ur_subscribe(&g_ui_ent, SIG_WIFI_STATUS);
    ur_subscribe(&g_ui_ent, SIG_PARAM_CHANGED);

    /* Configure ACL for audio entity */
    ur_acl_register(&g_audio_ent, audio_acl_rules,
                    sizeof(audio_acl_rules) / sizeof(audio_acl_rules[0]));
    ur_acl_enable_middleware(&g_audio_ent);

    /* Start entities */
    ur_start(&g_battery_ent);
    ur_start(&g_ui_ent);
    ur_start(&g_audio_ent);
    ur_start(&g_rpc_ent);

    /* Print initial state */
    printf("\nSubscriptions:\n");
    ur_bus_dump();
    printf("\nACL rules:\n");
    ur_acl_dump(&g_audio_ent);
    printf("\nParameters:\n");
    ur_param_dump();
    printf("\n");

    /* Create dispatch task */
    static ur_entity_t *entities[] = {
        &g_battery_ent, &g_ui_ent, &g_audio_ent, &g_rpc_ent
    };

    xTaskCreate(dispatch_task, "dispatch", 4096, entities, 5, NULL);
    xTaskCreate(battery_sim_task, "battery_sim", 2048, NULL, 3, NULL);

    /* Simulate some actions after 5 seconds */
    vTaskDelay(pdMS_TO_TICKS(5000));

    printf("\n--- Simulating audio play ---\n");
    ur_signal_t play_sig = UR_SIGNAL_U32(SIG_AUDIO_PLAY, ID_UI, 0);
    ur_emit(&g_audio_ent, play_sig);

    vTaskDelay(pdMS_TO_TICKS(3000));

    printf("\n--- Simulating volume change ---\n");
    ur_signal_t vol_sig = { .id = SIG_AUDIO_VOLUME, .src_id = ID_UI };
    vol_sig.payload.u8[0] = 75;
    ur_emit(&g_audio_ent, vol_sig);

    vTaskDelay(pdMS_TO_TICKS(3000));

    printf("\n--- Simulating external attack (should be blocked) ---\n");
    ur_signal_t attack_sig = { .id = SIG_FACTORY_RESET, .src_id = UR_ACL_SRC_EXTERNAL };
    ur_emit(&g_audio_ent, attack_sig);

    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("\n--- Simulating audio stop ---\n");
    ur_signal_t stop_sig = { .id = SIG_AUDIO_STOP, .src_id = ID_UI };
    ur_emit(&g_audio_ent, stop_sig);

    printf("\n--- Power state after stop ---\n");
    ur_power_dump();
}
