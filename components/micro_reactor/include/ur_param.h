/**
 * @file ur_param.h
 * @brief MicroReactor Persistent Parameter System (KV Store)
 *
 * Virtualized key-value storage with:
 * - Hardware abstraction (NVS, EEPROM, Flash)
 * - Auto-notification via pub/sub when parameters change
 * - Type-safe accessors for common types
 * - Dirty tracking for batch commits
 *
 * Usage:
 * 1. Define parameters with ur_param_def_t array
 * 2. Register parameters with ur_param_init()
 * 3. Read/write with ur_param_get_*/ur_param_set_*
 * 4. Subscribe to SIG_PARAM_CHANGED for notifications
 */

#ifndef UR_PARAM_H
#define UR_PARAM_H

#include "ur_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration
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

#ifndef UR_CFG_PARAM_NAMESPACE
#ifdef CONFIG_UR_PARAM_NAMESPACE
#define UR_CFG_PARAM_NAMESPACE      CONFIG_UR_PARAM_NAMESPACE
#else
#define UR_CFG_PARAM_NAMESPACE      "ur_param"
#endif
#endif

#if UR_CFG_PARAM_ENABLE

/* ============================================================================
 * System Signals for Parameters
 * ========================================================================== */

/**
 * @brief Parameter change notification signal
 *
 * Payload:
 * - payload.u16[0]: Parameter ID
 * - payload.u16[1]: Reserved
 * - ptr: Pointer to new value (optional)
 */
#define SIG_PARAM_CHANGED   0x0020

/**
 * @brief Parameter system ready signal
 */
#define SIG_PARAM_READY     0x0021

/* ============================================================================
 * Types
 * ========================================================================== */

/**
 * @brief Parameter types
 */
typedef enum {
    UR_PARAM_TYPE_U8 = 0,       /**< uint8_t */
    UR_PARAM_TYPE_U16,          /**< uint16_t */
    UR_PARAM_TYPE_U32,          /**< uint32_t */
    UR_PARAM_TYPE_I8,           /**< int8_t */
    UR_PARAM_TYPE_I16,          /**< int16_t */
    UR_PARAM_TYPE_I32,          /**< int32_t */
    UR_PARAM_TYPE_F32,          /**< float */
    UR_PARAM_TYPE_BOOL,         /**< bool (stored as u8) */
    UR_PARAM_TYPE_STR,          /**< String (null-terminated) */
    UR_PARAM_TYPE_BLOB,         /**< Binary blob */
} ur_param_type_t;

/**
 * @brief Parameter flags
 */
typedef enum {
    UR_PARAM_FLAG_NONE = 0x00,
    UR_PARAM_FLAG_PERSIST = 0x01,   /**< Save to flash on change */
    UR_PARAM_FLAG_READONLY = 0x02,  /**< Read-only parameter */
    UR_PARAM_FLAG_NOTIFY = 0x04,    /**< Send notification on change (default) */
    UR_PARAM_FLAG_DIRTY = 0x80,     /**< Internal: value changed, not saved */
} ur_param_flags_t;

/**
 * @brief Parameter value union
 */
typedef union {
    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
    int8_t   i8;
    int16_t  i16;
    int32_t  i32;
    float    f32;
    bool     b;
    char     str[UR_CFG_PARAM_MAX_STRING_LEN];
    uint8_t  blob[UR_CFG_PARAM_MAX_STRING_LEN];
} ur_param_value_t;

/**
 * @brief Parameter definition (compile-time)
 */
typedef struct {
    uint16_t id;                /**< Unique parameter ID */
    uint8_t type;               /**< ur_param_type_t */
    uint8_t flags;              /**< ur_param_flags_t */
    const char *name;           /**< Parameter name (for debug/NVS key) */
    uint16_t size;              /**< Size for STR/BLOB types */
    ur_param_value_t default_val; /**< Default value */
} ur_param_def_t;

/**
 * @brief Parameter runtime entry
 */
typedef struct {
    const ur_param_def_t *def;  /**< Pointer to definition */
    ur_param_value_t value;     /**< Current value */
    uint8_t flags;              /**< Runtime flags (dirty, etc.) */
    uint8_t _reserved[3];       /**< Padding */
} ur_param_entry_t;

/**
 * @brief Storage backend callbacks
 */
typedef struct {
    /**
     * @brief Initialize storage backend
     * @return UR_OK on success
     */
    ur_err_t (*init)(void);

    /**
     * @brief Load parameter from storage
     * @param key   Parameter name/key
     * @param type  Parameter type
     * @param value Output value
     * @param size  Max size for STR/BLOB
     * @return UR_OK on success, UR_ERR_NOT_FOUND if not stored
     */
    ur_err_t (*load)(const char *key, ur_param_type_t type,
                     ur_param_value_t *value, size_t size);

    /**
     * @brief Save parameter to storage
     * @param key   Parameter name/key
     * @param type  Parameter type
     * @param value Value to save
     * @param size  Size for STR/BLOB
     * @return UR_OK on success
     */
    ur_err_t (*save)(const char *key, ur_param_type_t type,
                     const ur_param_value_t *value, size_t size);

    /**
     * @brief Commit pending writes (for batch operations)
     * @return UR_OK on success
     */
    ur_err_t (*commit)(void);

    /**
     * @brief Erase all stored parameters
     * @return UR_OK on success
     */
    ur_err_t (*erase)(void);
} ur_param_storage_t;

/* ============================================================================
 * Initialization
 * ========================================================================== */

/**
 * @brief Initialize parameter system
 *
 * @param defs      Array of parameter definitions
 * @param count     Number of parameters
 * @param storage   Storage backend (NULL for RAM-only)
 * @return UR_OK on success
 *
 * @code
 * static const ur_param_def_t params[] = {
 *     { PARAM_VOLUME, UR_PARAM_TYPE_U8, UR_PARAM_FLAG_PERSIST,
 *       "volume", 0, { .u8 = 50 } },
 *     { PARAM_SSID, UR_PARAM_TYPE_STR, UR_PARAM_FLAG_PERSIST,
 *       "wifi_ssid", 32, { .str = "" } },
 * };
 * ur_param_init(params, 2, &nvs_storage);
 * @endcode
 */
ur_err_t ur_param_init(const ur_param_def_t *defs, size_t count,
                       const ur_param_storage_t *storage);

/**
 * @brief Load all persistent parameters from storage
 *
 * Called automatically by ur_param_init if storage is provided.
 *
 * @return Number of parameters loaded
 */
int ur_param_load_all(void);

/**
 * @brief Save all dirty parameters to storage
 *
 * @return Number of parameters saved
 */
int ur_param_save_all(void);

/**
 * @brief Reset all parameters to default values
 *
 * @param persist   Also erase from storage
 * @return UR_OK on success
 */
ur_err_t ur_param_reset_defaults(bool persist);

/* ============================================================================
 * Type-Safe Getters
 * ========================================================================== */

ur_err_t ur_param_get_u8(uint16_t id, uint8_t *value);
ur_err_t ur_param_get_u16(uint16_t id, uint16_t *value);
ur_err_t ur_param_get_u32(uint16_t id, uint32_t *value);
ur_err_t ur_param_get_i8(uint16_t id, int8_t *value);
ur_err_t ur_param_get_i16(uint16_t id, int16_t *value);
ur_err_t ur_param_get_i32(uint16_t id, int32_t *value);
ur_err_t ur_param_get_f32(uint16_t id, float *value);
ur_err_t ur_param_get_bool(uint16_t id, bool *value);
ur_err_t ur_param_get_str(uint16_t id, char *buffer, size_t max_len);
ur_err_t ur_param_get_blob(uint16_t id, void *buffer, size_t *len);

/* ============================================================================
 * Type-Safe Setters
 * ========================================================================== */

/**
 * @brief Set uint8_t parameter
 *
 * If parameter has PERSIST flag, value is saved to storage.
 * If parameter has NOTIFY flag (default), SIG_PARAM_CHANGED is published.
 *
 * @param id    Parameter ID
 * @param value New value
 * @return UR_OK on success
 */
ur_err_t ur_param_set_u8(uint16_t id, uint8_t value);
ur_err_t ur_param_set_u16(uint16_t id, uint16_t value);
ur_err_t ur_param_set_u32(uint16_t id, uint32_t value);
ur_err_t ur_param_set_i8(uint16_t id, int8_t value);
ur_err_t ur_param_set_i16(uint16_t id, int16_t value);
ur_err_t ur_param_set_i32(uint16_t id, int32_t value);
ur_err_t ur_param_set_f32(uint16_t id, float value);
ur_err_t ur_param_set_bool(uint16_t id, bool value);
ur_err_t ur_param_set_str(uint16_t id, const char *value);
ur_err_t ur_param_set_blob(uint16_t id, const void *data, size_t len);

/* ============================================================================
 * Batch Operations
 * ========================================================================== */

/**
 * @brief Begin batch update (defer persistence)
 *
 * During batch mode, changes are only written to RAM.
 * Call ur_param_commit() to persist all changes.
 */
void ur_param_batch_begin(void);

/**
 * @brief Commit batch updates to storage
 *
 * @return Number of parameters saved
 */
int ur_param_commit(void);

/**
 * @brief Abort batch update (discard changes)
 */
void ur_param_batch_abort(void);

/* ============================================================================
 * Query Functions
 * ========================================================================== */

/**
 * @brief Get parameter definition by ID
 *
 * @param id    Parameter ID
 * @return Pointer to definition, or NULL if not found
 */
const ur_param_def_t *ur_param_get_def(uint16_t id);

/**
 * @brief Get parameter count
 *
 * @return Number of registered parameters
 */
size_t ur_param_count(void);

/**
 * @brief Check if parameter exists
 *
 * @param id    Parameter ID
 * @return true if parameter is registered
 */
bool ur_param_exists(uint16_t id);

/**
 * @brief Check if parameter is dirty (changed but not saved)
 *
 * @param id    Parameter ID
 * @return true if dirty
 */
bool ur_param_is_dirty(uint16_t id);

/* ============================================================================
 * Debug Support
 * ========================================================================== */

/**
 * @brief Print all parameters (for debugging)
 */
void ur_param_dump(void);

/* ============================================================================
 * Built-in Storage Backends
 * ========================================================================== */

#if defined(CONFIG_IDF_TARGET) || defined(ESP_PLATFORM)
/**
 * @brief ESP-IDF NVS storage backend
 */
extern const ur_param_storage_t ur_param_storage_nvs;
#endif

/**
 * @brief RAM-only storage (no persistence, for testing)
 */
extern const ur_param_storage_t ur_param_storage_ram;

#endif /* UR_CFG_PARAM_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* UR_PARAM_H */
