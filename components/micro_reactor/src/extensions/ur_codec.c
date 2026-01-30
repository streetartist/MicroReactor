/**
 * @file ur_codec.c
 * @brief MicroReactor Signal Codec Implementation
 */

#include "ur_codec.h"
#include "ur_core.h"
#include "ur_utils.h"
#include <string.h>
#include <stdio.h>

#if UR_CFG_CODEC_ENABLE

/* ============================================================================
 * Internal State
 * ========================================================================== */

static struct {
    const ur_codec_schema_t *schemas[UR_CFG_CODEC_MAX_SCHEMAS];
    size_t schema_count;
    ur_rpc_recv_fn_t recv_callback;
    bool initialized;
} g_codec = { 0 };

/* ============================================================================
 * CRC16 (CCITT)
 * ========================================================================== */

static const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0,
};

uint16_t ur_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc << 8) ^ crc16_table[((crc >> 8) ^ data[i]) & 0xFF];
    }
    return crc;
}

/* ============================================================================
 * Initialization
 * ========================================================================== */

ur_err_t ur_codec_init(void)
{
    memset(&g_codec, 0, sizeof(g_codec));
    g_codec.initialized = true;

    UR_LOGD("Codec initialized (max_schemas=%d)", UR_CFG_CODEC_MAX_SCHEMAS);

    return UR_OK;
}

ur_err_t ur_codec_register_schema(const ur_codec_schema_t *schema)
{
    if (schema == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    if (!g_codec.initialized) {
        ur_codec_init();
    }

    if (g_codec.schema_count >= UR_CFG_CODEC_MAX_SCHEMAS) {
        return UR_ERR_NO_MEMORY;
    }

    /* Check for duplicate */
    for (size_t i = 0; i < g_codec.schema_count; i++) {
        if (g_codec.schemas[i]->signal_id == schema->signal_id) {
            return UR_ERR_ALREADY_EXISTS;
        }
    }

    g_codec.schemas[g_codec.schema_count++] = schema;

    UR_LOGD("Codec: registered schema for signal 0x%04X (%s)",
            schema->signal_id, schema->name ? schema->name : "unnamed");

    return UR_OK;
}

const ur_codec_schema_t *ur_codec_get_schema(uint16_t signal_id)
{
    for (size_t i = 0; i < g_codec.schema_count; i++) {
        if (g_codec.schemas[i]->signal_id == signal_id) {
            return g_codec.schemas[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * Binary Encoding
 * ========================================================================== */

ur_err_t ur_codec_encode_binary(const ur_signal_t *sig,
                                 uint8_t *buffer,
                                 size_t buf_size,
                                 size_t *out_len)
{
    if (sig == NULL || buffer == NULL || out_len == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    /* Get payload size from schema or use default */
    size_t payload_size = UR_CFG_SIGNAL_PAYLOAD_SIZE;
    const ur_codec_schema_t *schema = ur_codec_get_schema(sig->id);
    if (schema != NULL) {
        payload_size = schema->payload_size;
    }

    size_t total_size = UR_CODEC_HEADER_SIZE + payload_size + UR_CODEC_CRC_SIZE;

    if (buf_size < total_size) {
        return UR_ERR_NO_MEMORY;
    }

    /* Build header */
    size_t pos = 0;
    buffer[pos++] = UR_CODEC_SYNC_BYTE;
    buffer[pos++] = payload_size & 0xFF;
    buffer[pos++] = (payload_size >> 8) & 0xFF;
    buffer[pos++] = sig->id & 0xFF;
    buffer[pos++] = (sig->id >> 8) & 0xFF;
    buffer[pos++] = sig->src_id & 0xFF;
    buffer[pos++] = (sig->src_id >> 8) & 0xFF;

    /* Copy payload */
    memcpy(&buffer[pos], sig->payload.u8, UR_MIN(payload_size, UR_CFG_SIGNAL_PAYLOAD_SIZE));
    pos += payload_size;

    /* Calculate CRC (over header + payload, excluding sync byte) */
    uint16_t crc = ur_crc16(&buffer[1], pos - 1);
    buffer[pos++] = crc & 0xFF;
    buffer[pos++] = (crc >> 8) & 0xFF;

    *out_len = pos;

    return UR_OK;
}

/* ============================================================================
 * Binary Decoding
 * ========================================================================== */

ur_err_t ur_codec_decode_binary(const uint8_t *data,
                                 size_t len,
                                 ur_codec_decode_result_t *result)
{
    if (data == NULL || result == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(ur_codec_decode_result_t));

    /* Need at least header */
    if (len < UR_CODEC_HEADER_SIZE) {
        return UR_ERR_TIMEOUT;
    }

    /* Find sync byte */
    size_t start = 0;
    while (start < len && data[start] != UR_CODEC_SYNC_BYTE) {
        start++;
    }

    if (start >= len) {
        result->consumed = len;
        return UR_ERR_TIMEOUT;
    }

    /* Check if we have complete frame */
    if (len - start < UR_CODEC_HEADER_SIZE) {
        result->consumed = start;
        return UR_ERR_TIMEOUT;
    }

    const uint8_t *frame = &data[start];
    uint16_t payload_len = frame[1] | (frame[2] << 8);
    size_t total_len = UR_CODEC_HEADER_SIZE + payload_len + UR_CODEC_CRC_SIZE;

    if (len - start < total_len) {
        result->consumed = start;
        return UR_ERR_TIMEOUT;
    }

    /* Verify CRC */
    uint16_t expected_crc = frame[total_len - 2] | (frame[total_len - 1] << 8);
    uint16_t actual_crc = ur_crc16(&frame[1], total_len - 3);

    if (expected_crc != actual_crc) {
        UR_LOGW("Codec: CRC mismatch (expected 0x%04X, got 0x%04X)",
                expected_crc, actual_crc);
        result->consumed = start + 1;
        return UR_ERR_INVALID_ARG;
    }

    /* Parse signal */
    memset(&result->signal, 0, sizeof(ur_signal_t));
    result->signal.id = frame[3] | (frame[4] << 8);
    result->signal.src_id = frame[5] | (frame[6] << 8);

    /* Copy payload */
    size_t copy_len = UR_MIN(payload_len, UR_CFG_SIGNAL_PAYLOAD_SIZE);
    memcpy(result->signal.payload.u8, &frame[UR_CODEC_HEADER_SIZE], copy_len);

    result->consumed = start + total_len;
    result->complete = true;

    return UR_OK;
}

/* ============================================================================
 * Streaming Decoder
 * ========================================================================== */

void ur_codec_decoder_init(ur_codec_decoder_t *decoder)
{
    if (decoder != NULL) {
        memset(decoder, 0, sizeof(ur_codec_decoder_t));
    }
}

ur_err_t ur_codec_decoder_feed(ur_codec_decoder_t *decoder,
                                const uint8_t *data,
                                size_t len,
                                ur_signal_t *sig)
{
    if (decoder == NULL || data == NULL || sig == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];

        if (!decoder->in_frame) {
            if (byte == UR_CODEC_SYNC_BYTE) {
                decoder->in_frame = true;
                decoder->pos = 0;
                decoder->buffer[decoder->pos++] = byte;
            }
            continue;
        }

        /* Add byte to buffer */
        if (decoder->pos >= UR_CFG_CODEC_BUFFER_SIZE) {
            ur_codec_decoder_reset(decoder);
            continue;
        }

        decoder->buffer[decoder->pos++] = byte;

        /* Check if we have length */
        if (decoder->pos == 3) {
            decoder->expected_len = decoder->buffer[1] | (decoder->buffer[2] << 8);
            decoder->expected_len += UR_CODEC_HEADER_SIZE + UR_CODEC_CRC_SIZE;

            if (decoder->expected_len > UR_CFG_CODEC_BUFFER_SIZE) {
                ur_codec_decoder_reset(decoder);
                continue;
            }
        }

        /* Check if frame complete */
        if (decoder->pos >= UR_CODEC_HEADER_SIZE &&
            decoder->pos >= decoder->expected_len) {

            ur_codec_decode_result_t result;
            ur_err_t err = ur_codec_decode_binary(decoder->buffer, decoder->pos, &result);

            ur_codec_decoder_reset(decoder);

            if (err == UR_OK && result.complete) {
                *sig = result.signal;
                return UR_OK;
            }
        }
    }

    return UR_ERR_TIMEOUT;
}

void ur_codec_decoder_reset(ur_codec_decoder_t *decoder)
{
    if (decoder != NULL) {
        decoder->pos = 0;
        decoder->expected_len = 0;
        decoder->in_frame = false;
    }
}

/* ============================================================================
 * JSON Encoding (if enabled)
 * ========================================================================== */

#if UR_CFG_CODEC_JSON_ENABLE

ur_err_t ur_codec_encode_json(const ur_signal_t *sig,
                               char *buffer,
                               size_t buf_size,
                               size_t *out_len)
{
    if (sig == NULL || buffer == NULL || out_len == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    const ur_codec_schema_t *schema = ur_codec_get_schema(sig->id);

    int written;

    if (schema != NULL && schema->fields != NULL) {
        /* Use schema for structured encoding */
        written = snprintf(buffer, buf_size,
                          "{\"id\":%u,\"name\":\"%s\",\"src\":%u,\"ts\":%lu",
                          sig->id, schema->name ? schema->name : "",
                          sig->src_id, (unsigned long)sig->timestamp);

        if (written < 0 || (size_t)written >= buf_size) {
            return UR_ERR_NO_MEMORY;
        }

        size_t pos = written;

        /* Encode fields based on schema */
        for (size_t i = 0; i < schema->field_count && pos < buf_size - 20; i++) {
            const ur_codec_field_t *field = &schema->fields[i];
            const uint8_t *payload = sig->payload.u8;

            int field_written = 0;
            switch (field->type) {
                case UR_FIELD_U8:
                case UR_FIELD_BOOL:
                    field_written = snprintf(&buffer[pos], buf_size - pos,
                                             ",\"%s\":%u", field->name,
                                             payload[field->offset]);
                    break;
                case UR_FIELD_U16:
                    field_written = snprintf(&buffer[pos], buf_size - pos,
                                             ",\"%s\":%u", field->name,
                                             *(uint16_t *)&payload[field->offset]);
                    break;
                case UR_FIELD_U32:
                    field_written = snprintf(&buffer[pos], buf_size - pos,
                                             ",\"%s\":%lu", field->name,
                                             (unsigned long)*(uint32_t *)&payload[field->offset]);
                    break;
                case UR_FIELD_I8:
                    field_written = snprintf(&buffer[pos], buf_size - pos,
                                             ",\"%s\":%d", field->name,
                                             (int8_t)payload[field->offset]);
                    break;
                case UR_FIELD_I16:
                    field_written = snprintf(&buffer[pos], buf_size - pos,
                                             ",\"%s\":%d", field->name,
                                             *(int16_t *)&payload[field->offset]);
                    break;
                case UR_FIELD_I32:
                    field_written = snprintf(&buffer[pos], buf_size - pos,
                                             ",\"%s\":%ld", field->name,
                                             (long)*(int32_t *)&payload[field->offset]);
                    break;
                default:
                    break;
            }

            if (field_written > 0) {
                pos += field_written;
            }
        }

        /* Close object */
        if (pos < buf_size - 1) {
            buffer[pos++] = '}';
            buffer[pos] = '\0';
        }

        *out_len = pos;
    } else {
        /* No schema - encode raw payload */
        written = snprintf(buffer, buf_size,
                          "{\"id\":%u,\"src\":%u,\"ts\":%lu,\"payload\":[%u,%u,%u,%u]}",
                          sig->id, sig->src_id, (unsigned long)sig->timestamp,
                          sig->payload.u8[0], sig->payload.u8[1],
                          sig->payload.u8[2], sig->payload.u8[3]);

        if (written < 0 || (size_t)written >= buf_size) {
            return UR_ERR_NO_MEMORY;
        }

        *out_len = written;
    }

    return UR_OK;
}

ur_err_t ur_codec_decode_json(const char *json, ur_signal_t *sig)
{
    if (json == NULL || sig == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    memset(sig, 0, sizeof(ur_signal_t));

    /* Simple JSON parsing (minimal implementation) */
    const char *p = json;

    /* Find "id": */
    const char *id_str = strstr(p, "\"id\":");
    if (id_str != NULL) {
        sig->id = (uint16_t)atoi(id_str + 5);
    }

    /* Find "src": */
    const char *src_str = strstr(p, "\"src\":");
    if (src_str != NULL) {
        sig->src_id = (uint16_t)atoi(src_str + 6);
    }

    /* Find "ts": */
    const char *ts_str = strstr(p, "\"ts\":");
    if (ts_str != NULL) {
        sig->timestamp = (uint32_t)atol(ts_str + 5);
    }

    /* Find "payload": */
    const char *payload_str = strstr(p, "\"payload\":[");
    if (payload_str != NULL) {
        payload_str += 11;
        for (int i = 0; i < 4 && *payload_str; i++) {
            sig->payload.u8[i] = (uint8_t)atoi(payload_str);
            payload_str = strchr(payload_str, ',');
            if (payload_str) payload_str++;
            else break;
        }
    }

    /* If schema exists, try to decode named fields */
    const ur_codec_schema_t *schema = ur_codec_get_schema(sig->id);
    if (schema != NULL && schema->fields != NULL) {
        for (size_t i = 0; i < schema->field_count; i++) {
            const ur_codec_field_t *field = &schema->fields[i];
            char search[64];
            snprintf(search, sizeof(search), "\"%s\":", field->name);

            const char *val_str = strstr(p, search);
            if (val_str != NULL) {
                val_str += strlen(search);
                long val = atol(val_str);

                switch (field->type) {
                    case UR_FIELD_U8:
                    case UR_FIELD_BOOL:
                        sig->payload.u8[field->offset] = (uint8_t)val;
                        break;
                    case UR_FIELD_U16:
                        *(uint16_t *)&sig->payload.u8[field->offset] = (uint16_t)val;
                        break;
                    case UR_FIELD_U32:
                        *(uint32_t *)&sig->payload.u8[field->offset] = (uint32_t)val;
                        break;
                    case UR_FIELD_I8:
                        sig->payload.i8[field->offset] = (int8_t)val;
                        break;
                    case UR_FIELD_I16:
                        *(int16_t *)&sig->payload.u8[field->offset] = (int16_t)val;
                        break;
                    case UR_FIELD_I32:
                        *(int32_t *)&sig->payload.u8[field->offset] = (int32_t)val;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    return UR_OK;
}

#endif /* UR_CFG_CODEC_JSON_ENABLE */

/* ============================================================================
 * Generic Encode/Decode
 * ========================================================================== */

ur_err_t ur_codec_encode(const ur_signal_t *sig,
                          ur_codec_format_t format,
                          void *buffer,
                          size_t buf_size,
                          size_t *out_len)
{
    switch (format) {
        case UR_CODEC_BINARY:
            return ur_codec_encode_binary(sig, (uint8_t *)buffer, buf_size, out_len);

#if UR_CFG_CODEC_JSON_ENABLE
        case UR_CODEC_JSON:
            return ur_codec_encode_json(sig, (char *)buffer, buf_size, out_len);
#endif

        default:
            return UR_ERR_INVALID_ARG;
    }
}

/* ============================================================================
 * RPC Gateway
 * ========================================================================== */

void ur_rpc_set_recv_callback(ur_rpc_recv_fn_t callback)
{
    g_codec.recv_callback = callback;
}

ur_err_t ur_rpc_process(const void *data,
                         size_t len,
                         ur_codec_format_t format,
                         uint16_t target_id)
{
    if (data == NULL || len == 0) {
        return UR_ERR_INVALID_ARG;
    }

    ur_signal_t sig;
    ur_err_t err;

    switch (format) {
        case UR_CODEC_BINARY: {
            ur_codec_decode_result_t result;
            err = ur_codec_decode_binary((const uint8_t *)data, len, &result);
            if (err != UR_OK || !result.complete) {
                return err;
            }
            sig = result.signal;
            break;
        }

#if UR_CFG_CODEC_JSON_ENABLE
        case UR_CODEC_JSON:
            err = ur_codec_decode_json((const char *)data, &sig);
            if (err != UR_OK) {
                return err;
            }
            break;
#endif

        default:
            return UR_ERR_INVALID_ARG;
    }

    /* Route to target or callback */
    if (target_id != 0) {
        return ur_emit_to_id(target_id, sig);
    } else if (g_codec.recv_callback != NULL) {
        g_codec.recv_callback(&sig, NULL);
        return UR_OK;
    }

    return UR_ERR_NOT_FOUND;
}

/* ============================================================================
 * Debug
 * ========================================================================== */

void ur_codec_print_signal(const ur_signal_t *sig)
{
#if UR_CFG_ENABLE_LOGGING && UR_CFG_CODEC_JSON_ENABLE
    char buffer[256];
    size_t len;

    if (ur_codec_encode_json(sig, buffer, sizeof(buffer), &len) == UR_OK) {
        UR_LOGI("Signal: %s", buffer);
    }
#else
    (void)sig;
#endif
}

#endif /* UR_CFG_CODEC_ENABLE */
