/**
 * @file main.c
 * @brief MicroReactor Pipe Example - Audio-style Data Streaming
 *
 * Demonstrates:
 * - Pipe initialization with static buffer
 * - Producer-consumer pattern
 * - ISR-safe write operations
 * - Throughput monitoring
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ur_core.h"
#include "ur_pipe.h"
#include "ur_utils.h"

static const char *TAG = "pipe_example";

/* ============================================================================
 * Configuration
 * ========================================================================== */

#define SAMPLE_RATE_HZ      8000        /* Simulated audio sample rate */
#define SAMPLE_SIZE_BYTES   2           /* 16-bit samples */
#define PIPE_BUFFER_SIZE    1024        /* Pipe buffer size */
#define CHUNK_SIZE          64          /* Bytes per write/read */

/* ============================================================================
 * Pipe Definition
 * ========================================================================== */

/* Static pipe buffer */
static uint8_t audio_pipe_buffer[PIPE_BUFFER_SIZE];
static ur_pipe_t audio_pipe;

/* Statistics */
static volatile uint32_t g_samples_produced = 0;
static volatile uint32_t g_samples_consumed = 0;
static volatile uint32_t g_underruns = 0;
static volatile uint32_t g_overruns = 0;

/* ============================================================================
 * Producer: Simulates Audio ADC
 * ========================================================================== */

static void producer_task(void *pvParameters)
{
    (void)pvParameters;

    /* Simulated audio data - sine wave lookup table */
    static const int16_t sine_wave[16] = {
        0, 12539, 23170, 30273, 32767, 30273, 23170, 12539,
        0, -12539, -23170, -30273, -32767, -30273, -23170, -12539
    };

    int16_t samples[CHUNK_SIZE / SAMPLE_SIZE_BYTES];
    uint32_t phase = 0;

    ESP_LOGI(TAG, "[Producer] Started at %d Hz", SAMPLE_RATE_HZ);

    while (1) {
        /* Generate audio samples */
        for (size_t i = 0; i < CHUNK_SIZE / SAMPLE_SIZE_BYTES; i++) {
            samples[i] = sine_wave[phase % 16];
            phase++;
        }

        /* Write to pipe */
        size_t written = ur_pipe_write(&audio_pipe, samples, CHUNK_SIZE, 10);

        if (written < CHUNK_SIZE) {
            g_overruns++;
            if (g_overruns % 100 == 1) {
                ESP_LOGW(TAG, "[Producer] Overrun! Pipe full (total: %d)", g_overruns);
            }
        }

        g_samples_produced += written / SAMPLE_SIZE_BYTES;

        /* Pace the producer at sample rate */
        uint32_t delay_us = (CHUNK_SIZE / SAMPLE_SIZE_BYTES) * 1000000 / SAMPLE_RATE_HZ;
        vTaskDelay(pdMS_TO_TICKS(delay_us / 1000));
    }
}

/* ============================================================================
 * Consumer: Simulates Audio DAC
 * ========================================================================== */

static void consumer_task(void *pvParameters)
{
    (void)pvParameters;

    int16_t samples[CHUNK_SIZE / SAMPLE_SIZE_BYTES];
    uint32_t last_report_time = 0;

    ESP_LOGI(TAG, "[Consumer] Started");

    while (1) {
        /* Read from pipe */
        size_t read = ur_pipe_read(&audio_pipe, samples, CHUNK_SIZE, 20);

        if (read == 0) {
            g_underruns++;
            if (g_underruns % 100 == 1) {
                ESP_LOGW(TAG, "[Consumer] Underrun! Pipe empty (total: %d)", g_underruns);
            }
            continue;
        }

        g_samples_consumed += read / SAMPLE_SIZE_BYTES;

        /* Simulate DAC output - just compute some statistics */
        int32_t sum = 0;
        int16_t max_val = -32768;
        int16_t min_val = 32767;

        for (size_t i = 0; i < read / SAMPLE_SIZE_BYTES; i++) {
            sum += samples[i];
            if (samples[i] > max_val) max_val = samples[i];
            if (samples[i] < min_val) min_val = samples[i];
        }

        /* Periodic output report */
        uint32_t now = ur_time_ms();
        if (now - last_report_time >= 5000) {  /* Every 5 seconds */
            ESP_LOGI(TAG, "[Consumer] Last chunk: min=%d max=%d avg=%d",
                     min_val, max_val, (int)(sum / (read / SAMPLE_SIZE_BYTES)));
            last_report_time = now;
        }

        /* Pace the consumer slightly slower to test buffering */
        uint32_t delay_us = (read / SAMPLE_SIZE_BYTES) * 1000000 / SAMPLE_RATE_HZ;
        delay_us = delay_us * 105 / 100;  /* 5% slower */
        vTaskDelay(pdMS_TO_TICKS(delay_us / 1000));
    }
}

/* ============================================================================
 * Monitor Task: Reports Statistics
 * ========================================================================== */

static void monitor_task(void *pvParameters)
{
    (void)pvParameters;

    uint32_t last_produced = 0;
    uint32_t last_consumed = 0;
    uint32_t last_time = ur_time_ms();

    ESP_LOGI(TAG, "[Monitor] Started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));  /* Report every 2 seconds */

        uint32_t now = ur_time_ms();
        uint32_t elapsed = now - last_time;

        uint32_t produced = g_samples_produced;
        uint32_t consumed = g_samples_consumed;

        uint32_t prod_rate = (produced - last_produced) * 1000 / elapsed;
        uint32_t cons_rate = (consumed - last_consumed) * 1000 / elapsed;

        size_t available = ur_pipe_available(&audio_pipe);
        size_t space = ur_pipe_space(&audio_pipe);

        ESP_LOGI(TAG, "=====================================");
        ESP_LOGI(TAG, "Pipe: %d/%d bytes used (%d%% full)",
                 available, PIPE_BUFFER_SIZE,
                 (int)(available * 100 / PIPE_BUFFER_SIZE));
        ESP_LOGI(TAG, "Producer: %d samples/sec", prod_rate);
        ESP_LOGI(TAG, "Consumer: %d samples/sec", cons_rate);
        ESP_LOGI(TAG, "Total: produced=%d consumed=%d", produced, consumed);
        ESP_LOGI(TAG, "Errors: overruns=%d underruns=%d", g_overruns, g_underruns);
        ESP_LOGI(TAG, "=====================================");

        last_produced = produced;
        last_consumed = consumed;
        last_time = now;
    }
}

/* ============================================================================
 * ISR Producer Demo: Simulates Timer-driven ADC
 * ========================================================================== */

static esp_timer_handle_t isr_timer = NULL;
static volatile uint32_t g_isr_samples = 0;

static void IRAM_ATTR timer_isr_callback(void *arg)
{
    ur_pipe_t *pipe = (ur_pipe_t *)arg;

    /* Generate one sample in ISR */
    static int16_t phase = 0;
    int16_t sample = (phase++ % 2) ? 10000 : -10000;  /* Simple square wave */

    BaseType_t woken = pdFALSE;
    size_t written = ur_pipe_write_from_isr(pipe, &sample, sizeof(sample), &woken);

    if (written > 0) {
        g_isr_samples++;
    }

    /* Note: portYIELD_FROM_ISR not needed here as we're not waking a task */
    (void)woken;
}

static void start_isr_producer(void)
{
    /* Create a high-frequency timer to simulate ISR-driven data */
    esp_timer_create_args_t timer_args = {
        .callback = timer_isr_callback,
        .arg = &audio_pipe,
        .dispatch_method = ESP_TIMER_TASK,  /* Use ESP_TIMER_ISR for true ISR */
        .name = "adc_timer",
    };

    esp_timer_create(&timer_args, &isr_timer);

    /* Start at 1kHz (1ms period) - lower rate for demo stability */
    esp_timer_start_periodic(isr_timer, 1000);

    ESP_LOGI(TAG, "[ISR Producer] Started at 1000 Hz");
}

/* ============================================================================
 * Main
 * ========================================================================== */

void app_main(void)
{
    ESP_LOGI(TAG, "MicroReactor Pipe Example");
    ESP_LOGI(TAG, "Audio-style producer-consumer streaming");
    ESP_LOGI(TAG, "Pipe buffer: %d bytes, Chunk size: %d bytes",
             PIPE_BUFFER_SIZE, CHUNK_SIZE);

    /* Initialize the audio pipe */
    ur_err_t err = ur_pipe_init(&audio_pipe,
                                 audio_pipe_buffer,
                                 PIPE_BUFFER_SIZE,
                                 CHUNK_SIZE);  /* Trigger level = chunk size */

    if (err != UR_OK) {
        ESP_LOGE(TAG, "Failed to init pipe: %d", err);
        return;
    }

    ESP_LOGI(TAG, "Pipe initialized");
    ESP_LOGI(TAG, "  Size: %d bytes", ur_pipe_get_size(&audio_pipe));
    ESP_LOGI(TAG, "  Space: %d bytes", ur_pipe_space(&audio_pipe));
    ESP_LOGI(TAG, "  Empty: %s", ur_pipe_is_empty(&audio_pipe) ? "yes" : "no");

    /* Create producer and consumer tasks */
    xTaskCreate(producer_task, "producer", 4096, NULL, 5, NULL);
    xTaskCreate(consumer_task, "consumer", 4096, NULL, 5, NULL);
    xTaskCreate(monitor_task, "monitor", 4096, NULL, 3, NULL);

    /* Optionally start ISR-based producer after a delay */
    vTaskDelay(pdMS_TO_TICKS(10000));  /* Wait 10 seconds */
    ESP_LOGI(TAG, "Starting ISR producer (additional data source)...");
    start_isr_producer();

    /* Main loop - just monitor ISR samples */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "[ISR Producer] Total samples: %d", g_isr_samples);
    }
}
