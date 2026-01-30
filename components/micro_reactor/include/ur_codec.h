/**
 * @file ur_codec.h
 * @brief MicroReactor Signal Codec (Serialization/RPC)
 *
 * Provides bidirectional mapping between signals and byte streams:
 * - Binary format (TLV-style, compact, efficient)
 * - JSON format (human-readable, for debugging/external apps)
 * - Schema definition for signal payloads
 *
 * Use Cases:
 * - MQTT/HTTP communication with phone apps
 * - UART bridge to other microcontrollers
 * - Debug logging in readable format
 * - Signal persistence/replay
 */

#ifndef UR_CODEC_H
#define UR_CODEC_H

#include "ur_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration
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

#if UR_CFG_CODEC_ENABLE

/* ============================================================================
 * Types
 * ========================================================================== */

/**
 * @brief Field types for schema definition
 */
typedef enum {
    UR_FIELD_U8 = 0,        /**< uint8_t */
    UR_FIELD_U16,           /**< uint16_t */
    UR_FIELD_U32,           /**< uint32_t */
    UR_FIELD_I8,            /**< int8_t */
    UR_FIELD_I16,           /**< int16_t */
    UR_FIELD_I32,           /**< int32_t */
    UR_FIELD_F32,           /**< float */
    UR_FIELD_BOOL,          /**< bool (1 byte) */
    UR_FIELD_STR,           /**< Null-terminated string */
    UR_FIELD_BYTES,         /**< Raw bytes (length-prefixed) */
    UR_FIELD_ENUM,          /**< Enum (stored as u8) */
} ur_field_type_t;

/**
 * @brief Schema field definition
 */
typedef struct {
    const char *name;       /**< Field name (for JSON) */
    uint8_t type;           /**< ur_field_type_t */
    uint8_t offset;         /**< Offset in payload structure */
    uint8_t size;           /**< Size for STR/BYTES fields */
    uint8_t _reserved;      /**< Padding */
} ur_codec_field_t;

/**
 * @brief Signal schema definition
 *
 * Describes the payload structure for a signal ID.
 */
typedef struct {
    uint16_t signal_id;         /**< Signal ID this schema describes */
    const char *name;           /**< Signal name (for JSON) */
    const ur_codec_field_t *fields; /**< Array of field definitions */
    uint8_t field_count;        /**< Number of fields */
    uint8_t payload_size;       /**< Total payload size */
    uint8_t _reserved[2];       /**< Padding */
} ur_codec_schema_t;

/**
 * @brief Encoding format
 */
typedef enum {
    UR_CODEC_BINARY = 0,    /**< Compact binary (TLV) */
    UR_CODEC_JSON,          /**< JSON format */
} ur_codec_format_t;

/**
 * @brief Binary frame header (12 bytes)
 *
 * Frame format:
 * [SYNC:1][LEN:2][SIG_ID:2][SRC_ID:2][PAYLOAD:N][CRC16:2]
 */
#define UR_CODEC_SYNC_BYTE      0x55
#define UR_CODEC_HEADER_SIZE    7
#define UR_CODEC_CRC_SIZE       2

typedef struct {
    uint8_t sync;           /**< Sync byte (0x55) */
    uint16_t length;        /**< Payload length */
    uint16_t signal_id;     /**< Signal ID */
    uint16_t src_id;        /**< Source entity ID */
} __attribute__((packed)) ur_codec_header_t;

/**
 * @brief Decode result
 */
typedef struct {
    ur_signal_t signal;     /**< Decoded signal */
    size_t consumed;        /**< Bytes consumed from input */
    bool complete;          /**< True if a complete frame was decoded */
} ur_codec_decode_result_t;

/* ============================================================================
 * Initialization
 * ========================================================================== */

/**
 * @brief Initialize codec system
 *
 * @return UR_OK on success
 */
ur_err_t ur_codec_init(void);

/**
 * @brief Register a signal schema
 *
 * Schema enables structured encoding/decoding of signal payloads.
 * Signals without schemas use raw payload bytes.
 *
 * @param schema    Schema definition
 * @return UR_OK on success
 *
 * @code
 * static const ur_codec_field_t play_cmd_fields[] = {
 *     { "volume", UR_FIELD_U8, 0, 0 },
 *     { "song_id", UR_FIELD_U16, 1, 0 },
 * };
 * static const ur_codec_schema_t play_cmd_schema = {
 *     .signal_id = SIG_CMD_PLAY,
 *     .name = "play",
 *     .fields = play_cmd_fields,
 *     .field_count = 2,
 *     .payload_size = 3,
 * };
 * ur_codec_register_schema(&play_cmd_schema);
 * @endcode
 */
ur_err_t ur_codec_register_schema(const ur_codec_schema_t *schema);

/**
 * @brief Get schema for a signal ID
 *
 * @param signal_id Signal ID
 * @return Schema pointer or NULL if not registered
 */
const ur_codec_schema_t *ur_codec_get_schema(uint16_t signal_id);

/* ============================================================================
 * Encoding
 * ========================================================================== */

/**
 * @brief Encode a signal to binary format
 *
 * @param sig       Signal to encode
 * @param buffer    Output buffer
 * @param buf_size  Buffer size
 * @param out_len   Output: encoded length
 * @return UR_OK on success
 */
ur_err_t ur_codec_encode_binary(const ur_signal_t *sig,
                                 uint8_t *buffer,
                                 size_t buf_size,
                                 size_t *out_len);

#if UR_CFG_CODEC_JSON_ENABLE
/**
 * @brief Encode a signal to JSON format
 *
 * @param sig       Signal to encode
 * @param buffer    Output buffer (null-terminated)
 * @param buf_size  Buffer size
 * @param out_len   Output: encoded length (excluding null)
 * @return UR_OK on success
 *
 * Example output:
 * {"id":256,"src":1,"ts":12345,"vol":80,"song_id":42}
 */
ur_err_t ur_codec_encode_json(const ur_signal_t *sig,
                               char *buffer,
                               size_t buf_size,
                               size_t *out_len);
#endif

/**
 * @brief Encode signal with specified format
 *
 * @param sig       Signal to encode
 * @param format    Encoding format
 * @param buffer    Output buffer
 * @param buf_size  Buffer size
 * @param out_len   Output: encoded length
 * @return UR_OK on success
 */
ur_err_t ur_codec_encode(const ur_signal_t *sig,
                          ur_codec_format_t format,
                          void *buffer,
                          size_t buf_size,
                          size_t *out_len);

/* ============================================================================
 * Decoding
 * ========================================================================== */

/**
 * @brief Decode binary frame to signal
 *
 * @param data      Input data
 * @param len       Input length
 * @param result    Output: decoded signal and consumed bytes
 * @return UR_OK if frame decoded, UR_ERR_TIMEOUT if need more data
 */
ur_err_t ur_codec_decode_binary(const uint8_t *data,
                                 size_t len,
                                 ur_codec_decode_result_t *result);

#if UR_CFG_CODEC_JSON_ENABLE
/**
 * @brief Decode JSON string to signal
 *
 * @param json      JSON string (null-terminated)
 * @param sig       Output: decoded signal
 * @return UR_OK on success
 */
ur_err_t ur_codec_decode_json(const char *json, ur_signal_t *sig);
#endif

/**
 * @brief Streaming decoder for binary frames
 *
 * Feed data byte-by-byte or in chunks. Calls callback when
 * a complete frame is decoded.
 */
typedef struct {
    uint8_t buffer[UR_CFG_CODEC_BUFFER_SIZE];
    size_t pos;
    size_t expected_len;
    bool in_frame;
} ur_codec_decoder_t;

/**
 * @brief Initialize streaming decoder
 *
 * @param decoder   Decoder state
 */
void ur_codec_decoder_init(ur_codec_decoder_t *decoder);

/**
 * @brief Feed data to streaming decoder
 *
 * @param decoder   Decoder state
 * @param data      Input data
 * @param len       Input length
 * @param sig       Output: decoded signal (valid when returns UR_OK)
 * @return UR_OK if signal decoded, UR_ERR_TIMEOUT if need more data,
 *         UR_ERR_INVALID_ARG if frame error (decoder resets)
 */
ur_err_t ur_codec_decoder_feed(ur_codec_decoder_t *decoder,
                                const uint8_t *data,
                                size_t len,
                                ur_signal_t *sig);

/**
 * @brief Reset streaming decoder
 *
 * @param decoder   Decoder state
 */
void ur_codec_decoder_reset(ur_codec_decoder_t *decoder);

/* ============================================================================
 * RPC Gateway
 * ========================================================================== */

/**
 * @brief RPC receive callback
 *
 * Called when a signal is received from external source.
 *
 * @param sig       Received signal
 * @param source    Source identifier (e.g., UART port, MQTT topic)
 */
typedef void (*ur_rpc_recv_fn_t)(const ur_signal_t *sig, void *source);

/**
 * @brief Register RPC receive callback
 *
 * @param callback  Callback function
 */
void ur_rpc_set_recv_callback(ur_rpc_recv_fn_t callback);

/**
 * @brief Process received data as RPC
 *
 * Decodes and optionally injects signal into target entity.
 *
 * @param data      Received data
 * @param len       Data length
 * @param format    Data format
 * @param target_id Target entity ID (0 = use callback)
 * @return UR_OK if signal processed
 */
ur_err_t ur_rpc_process(const void *data,
                         size_t len,
                         ur_codec_format_t format,
                         uint16_t target_id);

/* ============================================================================
 * Utility
 * ========================================================================== */

/**
 * @brief Calculate CRC16 (CCITT)
 *
 * @param data  Data buffer
 * @param len   Data length
 * @return CRC16 value
 */
uint16_t ur_crc16(const uint8_t *data, size_t len);

/**
 * @brief Print signal as JSON (for debugging)
 *
 * @param sig   Signal to print
 */
void ur_codec_print_signal(const ur_signal_t *sig);

#endif /* UR_CFG_CODEC_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* UR_CODEC_H */
