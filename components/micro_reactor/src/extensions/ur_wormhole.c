/**
 * @file ur_wormhole.c
 * @brief MicroReactor cross-chip RPC (Wormhole)
 *
 * Provides distributed signal routing over UART.
 * Protocol: | 0xAA (1B) | SrcID (2B) | SigID (2B) | Payload (4B) | CRC8 (1B) |
 */

#include "ur_core.h"
#include "ur_utils.h"

#if UR_CFG_WORMHOLE_ENABLE

#include <string.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ============================================================================
 * Constants
 * ========================================================================== */

#define WORMHOLE_SYNC_BYTE      0xAA
#define WORMHOLE_FRAME_SIZE     10
#define WORMHOLE_RX_BUF_SIZE    256
#define WORMHOLE_TX_BUF_SIZE    128

/* ============================================================================
 * Static Data
 * ========================================================================== */

static ur_wormhole_route_t g_routes[UR_CFG_WORMHOLE_MAX_ROUTES];
static size_t g_route_count = 0;
static TaskHandle_t g_rx_task_handle = NULL;
static bool g_wormhole_initialized = false;
static uint8_t g_local_chip_id = 0;

/* RX task stack (static allocation) */
static StackType_t g_rx_task_stack[UR_CFG_WORMHOLE_RX_TASK_STACK / sizeof(StackType_t)];
static StaticTask_t g_rx_task_tcb;

/* ============================================================================
 * CRC8 Implementation
 * ========================================================================== */

static const uint8_t crc8_table[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};

uint8_t ur_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;

    for (size_t i = 0; i < len; i++) {
        crc = crc8_table[crc ^ data[i]];
    }

    return crc;
}

uint8_t ur_crc8_update(uint8_t crc, uint8_t byte)
{
    return crc8_table[crc ^ byte];
}

/* ============================================================================
 * Frame Serialization
 * ========================================================================== */

static void serialize_frame(uint8_t *buf, uint16_t src_id, uint16_t sig_id, uint32_t payload)
{
    buf[0] = WORMHOLE_SYNC_BYTE;
    buf[1] = (uint8_t)(src_id & 0xFF);
    buf[2] = (uint8_t)(src_id >> 8);
    buf[3] = (uint8_t)(sig_id & 0xFF);
    buf[4] = (uint8_t)(sig_id >> 8);
    buf[5] = (uint8_t)(payload & 0xFF);
    buf[6] = (uint8_t)((payload >> 8) & 0xFF);
    buf[7] = (uint8_t)((payload >> 16) & 0xFF);
    buf[8] = (uint8_t)((payload >> 24) & 0xFF);
    buf[9] = ur_crc8(&buf[1], 8);  /* CRC covers bytes 1-8 */
}

static bool deserialize_frame(const uint8_t *buf, uint16_t *src_id, uint16_t *sig_id, uint32_t *payload)
{
    if (buf[0] != WORMHOLE_SYNC_BYTE) {
        return false;
    }

    /* Verify CRC */
    uint8_t crc = ur_crc8(&buf[1], 8);
    if (crc != buf[9]) {
        UR_LOGW("[WORMHOLE] CRC mismatch: expected 0x%02X, got 0x%02X", buf[9], crc);
        return false;
    }

    *src_id = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
    *sig_id = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);
    *payload = (uint32_t)buf[5] |
               ((uint32_t)buf[6] << 8) |
               ((uint32_t)buf[7] << 16) |
               ((uint32_t)buf[8] << 24);

    return true;
}

/* ============================================================================
 * Route Management
 * ========================================================================== */

ur_err_t ur_wormhole_add_route(uint16_t local_id, uint16_t remote_id, uint8_t channel)
{
    if (g_route_count >= UR_CFG_WORMHOLE_MAX_ROUTES) {
        return UR_ERR_NO_MEMORY;
    }

    /* Check for duplicate */
    for (size_t i = 0; i < g_route_count; i++) {
        if (g_routes[i].entity_id == local_id && g_routes[i].remote_id == remote_id) {
            return UR_ERR_ALREADY_EXISTS;
        }
    }

    g_routes[g_route_count].entity_id = local_id;
    g_routes[g_route_count].remote_id = remote_id;
    g_routes[g_route_count].channel = channel;
    g_routes[g_route_count].flags = 0;
    g_route_count++;

    UR_LOGD("[WORMHOLE] Route added: local=%d <-> remote=%d on ch%d",
            local_id, remote_id, channel);

    return UR_OK;
}

ur_err_t ur_wormhole_remove_route(uint16_t local_id, uint16_t remote_id)
{
    for (size_t i = 0; i < g_route_count; i++) {
        if (g_routes[i].entity_id == local_id && g_routes[i].remote_id == remote_id) {
            /* Shift remaining routes */
            for (size_t j = i; j < g_route_count - 1; j++) {
                g_routes[j] = g_routes[j + 1];
            }
            g_route_count--;
            return UR_OK;
        }
    }

    return UR_ERR_NOT_FOUND;
}

static const ur_wormhole_route_t *find_route_by_remote(uint16_t remote_id)
{
    for (size_t i = 0; i < g_route_count; i++) {
        if (g_routes[i].remote_id == remote_id) {
            return &g_routes[i];
        }
    }
    return NULL;
}

static const ur_wormhole_route_t *find_route_by_local(uint16_t local_id)
{
    for (size_t i = 0; i < g_route_count; i++) {
        if (g_routes[i].entity_id == local_id) {
            return &g_routes[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * TX Functions
 * ========================================================================== */

ur_err_t ur_wormhole_send(uint16_t remote_id, ur_signal_t sig)
{
    const ur_wormhole_route_t *route = find_route_by_remote(remote_id);
    if (route == NULL) {
        return UR_ERR_NOT_FOUND;
    }

    uint8_t frame[WORMHOLE_FRAME_SIZE];
    serialize_frame(frame, sig.src_id, sig.id, sig.payload.u32[0]);

    int written = uart_write_bytes(route->channel, frame, WORMHOLE_FRAME_SIZE);
    if (written != WORMHOLE_FRAME_SIZE) {
        UR_LOGW("[WORMHOLE] TX failed: wrote %d/%d bytes", written, WORMHOLE_FRAME_SIZE);
        return UR_ERR_TIMEOUT;
    }

    UR_LOGV("[WORMHOLE] TX: sig=0x%04X -> remote=%d", sig.id, remote_id);

    return UR_OK;
}

/**
 * @brief Wormhole TX middleware
 *
 * Intercepts signals destined for remote entities and routes via UART.
 */
ur_mw_result_t ur_mw_wormhole_tx(ur_entity_t *ent, ur_signal_t *sig, void *ctx)
{
    (void)ctx;

    /* Check if target entity is remote */
    const ur_wormhole_route_t *route = find_route_by_local(ent->id);
    if (route == NULL) {
        /* Not a routed entity - continue normal processing */
        return UR_MW_CONTINUE;
    }

    /* Send via wormhole */
    ur_wormhole_send(route->remote_id, *sig);

    /* Signal handled via remote transmission */
    return UR_MW_HANDLED;
}

/* ============================================================================
 * RX Task
 * ========================================================================== */

static void wormhole_rx_task(void *pvParameters)
{
    (void)pvParameters;

    uint8_t rx_buf[WORMHOLE_RX_BUF_SIZE];
    uint8_t frame_buf[WORMHOLE_FRAME_SIZE];
    size_t frame_idx = 0;
    bool sync_found = false;

    UR_LOGI("[WORMHOLE] RX task started on UART%d", UR_CFG_WORMHOLE_UART_NUM);

    while (1) {
        int len = uart_read_bytes(UR_CFG_WORMHOLE_UART_NUM, rx_buf,
                                   WORMHOLE_RX_BUF_SIZE, pdMS_TO_TICKS(100));

        for (int i = 0; i < len; i++) {
            uint8_t byte = rx_buf[i];

            if (!sync_found) {
                if (byte == WORMHOLE_SYNC_BYTE) {
                    sync_found = true;
                    frame_buf[0] = byte;
                    frame_idx = 1;
                }
                continue;
            }

            frame_buf[frame_idx++] = byte;

            if (frame_idx == WORMHOLE_FRAME_SIZE) {
                /* Complete frame received */
                uint16_t src_id, sig_id;
                uint32_t payload;

                if (deserialize_frame(frame_buf, &src_id, &sig_id, &payload)) {
                    /* Find local entity for this source */
                    const ur_wormhole_route_t *route = find_route_by_remote(src_id);
                    if (route != NULL) {
                        ur_entity_t *target = ur_get_entity(route->entity_id);
                        if (target != NULL) {
                            ur_signal_t sig = {
                                .id = sig_id,
                                .src_id = src_id,
                                .payload.u32[0] = payload,
                                .timestamp = ur_get_time_ms()
                            };
                            ur_emit(target, sig);
                            UR_LOGV("[WORMHOLE] RX: sig=0x%04X from remote=%d -> local=%d",
                                    sig_id, src_id, route->entity_id);
                        }
                    }
                }

                /* Reset for next frame */
                sync_found = false;
                frame_idx = 0;
            }
        }
    }
}

/* ============================================================================
 * Initialization
 * ========================================================================== */

ur_err_t ur_wormhole_init(uint8_t chip_id)
{
    if (g_wormhole_initialized) {
        return UR_ERR_ALREADY_EXISTS;
    }

    g_local_chip_id = chip_id;

    /* Configure UART */
    uart_config_t uart_config = {
        .baud_rate = UR_CFG_WORMHOLE_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(UR_CFG_WORMHOLE_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        UR_LOGE("[WORMHOLE] UART config failed: %d", err);
        return UR_ERR_INVALID_ARG;
    }

    err = uart_driver_install(UR_CFG_WORMHOLE_UART_NUM,
                              WORMHOLE_RX_BUF_SIZE * 2,
                              WORMHOLE_TX_BUF_SIZE,
                              0, NULL, 0);
    if (err != ESP_OK) {
        UR_LOGE("[WORMHOLE] UART driver install failed: %d", err);
        return UR_ERR_NO_MEMORY;
    }

    /* Create RX task */
    g_rx_task_handle = xTaskCreateStatic(
        wormhole_rx_task,
        "wormhole_rx",
        UR_CFG_WORMHOLE_RX_TASK_STACK / sizeof(StackType_t),
        NULL,
        UR_CFG_WORMHOLE_RX_TASK_PRIORITY,
        g_rx_task_stack,
        &g_rx_task_tcb
    );

    if (g_rx_task_handle == NULL) {
        uart_driver_delete(UR_CFG_WORMHOLE_UART_NUM);
        return UR_ERR_NO_MEMORY;
    }

    g_wormhole_initialized = true;

    UR_LOGI("[WORMHOLE] Initialized on UART%d @ %d baud, chip_id=%d",
            UR_CFG_WORMHOLE_UART_NUM, UR_CFG_WORMHOLE_BAUD_RATE, chip_id);

    return UR_OK;
}

ur_err_t ur_wormhole_deinit(void)
{
    if (!g_wormhole_initialized) {
        return UR_ERR_INVALID_STATE;
    }

    /* Delete RX task */
    if (g_rx_task_handle != NULL) {
        vTaskDelete(g_rx_task_handle);
        g_rx_task_handle = NULL;
    }

    /* Delete UART driver */
    uart_driver_delete(UR_CFG_WORMHOLE_UART_NUM);

    /* Clear routes */
    g_route_count = 0;

    g_wormhole_initialized = false;

    return UR_OK;
}

uint8_t ur_wormhole_get_chip_id(void)
{
    return g_local_chip_id;
}

#endif /* UR_CFG_WORMHOLE_ENABLE */
