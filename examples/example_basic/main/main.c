/**
 * @file main.c
 * @brief MicroReactor Basic Example - LED Blinker with Button Control
 *
 * Demonstrates:
 * - Entity initialization
 * - Signal emission
 * - FSM state transitions
 * - uFlow coroutine timing
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "ur_core.h"
#include "ur_flow.h"
#include "ur_utils.h"

static const char *TAG = "basic_example";

/* ============================================================================
 * Hardware Configuration
 * ========================================================================== */

#define LED_GPIO        GPIO_NUM_2      /* Built-in LED on most ESP32 boards */
#define BUTTON_GPIO     GPIO_NUM_0      /* Boot button */

/* ============================================================================
 * Signal Definitions
 * ========================================================================== */

enum {
    SIG_BUTTON_PRESS = SIG_USER_BASE,
    SIG_BUTTON_RELEASE,
    SIG_TOGGLE_MODE,
    SIG_TICK,
};

/* ============================================================================
 * State Definitions
 * ========================================================================== */

enum {
    STATE_IDLE = 1,
    STATE_BLINKING,
    STATE_SOLID_ON,
};

/* ============================================================================
 * Scratchpad for uFlow
 * ========================================================================== */

typedef struct {
    uint32_t blink_count;
    bool led_state;
} blink_scratch_t;

UR_SCRATCH_STATIC_ASSERT(blink_scratch_t);

/* ============================================================================
 * LED Entity
 * ========================================================================== */

static ur_entity_t led_entity;

/* Forward declarations */
static uint16_t action_start_blinking(ur_entity_t *ent, const ur_signal_t *sig);
static uint16_t action_stop_blinking(ur_entity_t *ent, const ur_signal_t *sig);
static uint16_t action_toggle_led(ur_entity_t *ent, const ur_signal_t *sig);
static uint16_t action_led_on(ur_entity_t *ent, const ur_signal_t *sig);
static uint16_t action_led_off(ur_entity_t *ent, const ur_signal_t *sig);
static uint16_t action_blink_flow(ur_entity_t *ent, const ur_signal_t *sig);

/* State: IDLE */
static const ur_rule_t idle_rules[] = {
    UR_RULE(SIG_BUTTON_PRESS, STATE_BLINKING, action_start_blinking),
    UR_RULE(SIG_TOGGLE_MODE, STATE_SOLID_ON, action_led_on),
    UR_RULE_END
};

/* State: BLINKING */
static const ur_rule_t blinking_rules[] = {
    UR_RULE(SIG_BUTTON_PRESS, STATE_IDLE, action_stop_blinking),
    UR_RULE(SIG_TICK, 0, action_blink_flow),
    UR_RULE(SIG_TOGGLE_MODE, STATE_SOLID_ON, action_led_on),
    UR_RULE_END
};

/* State: SOLID_ON */
static const ur_rule_t solid_on_rules[] = {
    UR_RULE(SIG_BUTTON_PRESS, STATE_IDLE, action_led_off),
    UR_RULE(SIG_TOGGLE_MODE, STATE_BLINKING, action_start_blinking),
    UR_RULE_END
};

/* State definitions */
static const ur_state_def_t led_states[] = {
    UR_STATE(STATE_IDLE, 0, NULL, NULL, idle_rules),
    UR_STATE(STATE_BLINKING, 0, NULL, NULL, blinking_rules),
    UR_STATE(STATE_SOLID_ON, 0, NULL, NULL, solid_on_rules),
};

/* ============================================================================
 * Action Implementations
 * ========================================================================== */

static void set_led(bool on)
{
    gpio_set_level(LED_GPIO, on ? 1 : 0);
}

static uint16_t action_start_blinking(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)sig;
    ESP_LOGI(TAG, "Starting blink mode");

    blink_scratch_t *s = UR_SCRATCH_PTR(ent, blink_scratch_t);
    s->blink_count = 0;
    s->led_state = false;
    ent->flow_line = 0;  /* Reset flow */

    return 0;
}

static uint16_t action_stop_blinking(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)ent;
    (void)sig;
    ESP_LOGI(TAG, "Stopping blink mode");
    set_led(false);
    return 0;
}

static uint16_t action_toggle_led(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)sig;
    blink_scratch_t *s = UR_SCRATCH_PTR(ent, blink_scratch_t);
    s->led_state = !s->led_state;
    set_led(s->led_state);
    return 0;
}

static uint16_t action_led_on(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)ent;
    (void)sig;
    ESP_LOGI(TAG, "LED solid ON");
    set_led(true);
    return 0;
}

static uint16_t action_led_off(ur_entity_t *ent, const ur_signal_t *sig)
{
    (void)ent;
    (void)sig;
    ESP_LOGI(TAG, "LED OFF");
    set_led(false);
    return 0;
}

/**
 * @brief Blink action using uFlow coroutine
 *
 * This demonstrates the uFlow coroutine pattern for timing.
 */
static uint16_t action_blink_flow(ur_entity_t *ent, const ur_signal_t *sig)
{
    blink_scratch_t *s = UR_SCRATCH_PTR(ent, blink_scratch_t);

    UR_FLOW_BEGIN(ent);

    while (1) {
        /* LED ON */
        s->led_state = true;
        set_led(true);
        s->blink_count++;

        /* Wait for next tick (500ms handled by tick task) */
        UR_AWAIT_SIGNAL(ent, SIG_TICK);

        /* LED OFF */
        s->led_state = false;
        set_led(false);

        /* Wait for next tick */
        UR_AWAIT_SIGNAL(ent, SIG_TICK);
    }

    UR_FLOW_END(ent);
}

/* ============================================================================
 * GPIO Interrupt Handler
 * ========================================================================== */

static void IRAM_ATTR button_isr_handler(void *arg)
{
    ur_entity_t *ent = (ur_entity_t *)arg;
    BaseType_t woken = pdFALSE;

    int level = gpio_get_level(BUTTON_GPIO);

    ur_signal_t sig = {
        .id = level ? SIG_BUTTON_RELEASE : SIG_BUTTON_PRESS,
        .src_id = 0,
    };

    ur_emit_from_isr(ent, sig, &woken);

    portYIELD_FROM_ISR(woken);
}

/* ============================================================================
 * Tick Task
 * ========================================================================== */

static void tick_task(void *pvParameters)
{
    ur_entity_t *ent = (ur_entity_t *)pvParameters;

    while (1) {
        /* Send tick signal every 500ms when blinking */
        if (ur_in_state(ent, STATE_BLINKING)) {
            ur_signal_t tick = ur_signal_create(SIG_TICK, 0);
            ur_emit(ent, tick);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ============================================================================
 * Dispatch Task
 * ========================================================================== */

static void dispatch_task(void *pvParameters)
{
    ur_entity_t *ent = (ur_entity_t *)pvParameters;

    ESP_LOGI(TAG, "Dispatch task started");

    while (1) {
        /* Block until a signal is available, then process it */
        ur_dispatch(ent, portMAX_DELAY);
    }
}

/* ============================================================================
 * Hardware Init
 * ========================================================================== */

static void init_gpio(void)
{
    /* Configure LED */
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);
    set_led(false);

    /* Configure button with interrupt */
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&btn_conf);

    /* Install ISR service */
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, &led_entity);
}

/* ============================================================================
 * Main Entry
 * ========================================================================== */

void app_main(void)
{
    ESP_LOGI(TAG, "MicroReactor Basic Example");
    ESP_LOGI(TAG, "Press button to cycle: IDLE -> BLINKING -> IDLE");

    /* Initialize GPIO */
    init_gpio();

    /* Initialize LED entity */
    ur_entity_config_t led_config = {
        .id = 1,
        .name = "LED",
        .states = led_states,
        .state_count = sizeof(led_states) / sizeof(led_states[0]),
        .initial_state = STATE_IDLE,
        .user_data = NULL,
    };

    ur_err_t err = ur_init(&led_entity, &led_config);
    if (err != UR_OK) {
        ESP_LOGE(TAG, "Failed to init LED entity: %d", err);
        return;
    }

    /* Register entity in global registry */
    ur_register_entity(&led_entity);

    /* Start the entity */
    err = ur_start(&led_entity);
    if (err != UR_OK) {
        ESP_LOGE(TAG, "Failed to start LED entity: %d", err);
        return;
    }

    ESP_LOGI(TAG, "LED entity started in state %d", ur_get_state(&led_entity));

    /* Create dispatch task */
    xTaskCreate(dispatch_task, "dispatch", 4096, &led_entity, 5, NULL);

    /* Create tick task for blink timing */
    xTaskCreate(tick_task, "tick", 2048, &led_entity, 4, NULL);

    ESP_LOGI(TAG, "System running. Press boot button to control LED.");
}
