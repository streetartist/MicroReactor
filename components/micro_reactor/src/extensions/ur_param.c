/**
 * @file ur_param.c
 * @brief MicroReactor Persistent Parameter System Implementation
 */

#include "ur_param.h"
#include "ur_bus.h"
#include "ur_utils.h"
#include <string.h>

#if UR_CFG_PARAM_ENABLE

/* ============================================================================
 * Internal State
 * ========================================================================== */

static struct {
    ur_param_entry_t entries[UR_CFG_PARAM_MAX_COUNT];
    size_t count;
    const ur_param_storage_t *storage;
    bool batch_mode;
    bool initialized;
} g_param = { 0 };

/* ============================================================================
 * Internal Functions
 * ========================================================================== */

static ur_param_entry_t *find_entry(uint16_t id)
{
    for (size_t i = 0; i < g_param.count; i++) {
        if (g_param.entries[i].def != NULL &&
            g_param.entries[i].def->id == id) {
            return &g_param.entries[i];
        }
    }
    return NULL;
}

static size_t get_type_size(ur_param_type_t type)
{
    switch (type) {
        case UR_PARAM_TYPE_U8:
        case UR_PARAM_TYPE_I8:
        case UR_PARAM_TYPE_BOOL:
            return 1;
        case UR_PARAM_TYPE_U16:
        case UR_PARAM_TYPE_I16:
            return 2;
        case UR_PARAM_TYPE_U32:
        case UR_PARAM_TYPE_I32:
        case UR_PARAM_TYPE_F32:
            return 4;
        default:
            return 0;
    }
}

static void notify_change(uint16_t param_id, const ur_param_value_t *value)
{
#if UR_CFG_BUS_ENABLE
    ur_signal_t sig = {
        .id = SIG_PARAM_CHANGED,
        .src_id = 0,
        .payload.u16 = { param_id, 0 },
        .ptr = (void *)value,
        .timestamp = 0
    };
    ur_publish(sig);
#else
    (void)param_id;
    (void)value;
#endif
}

static ur_err_t save_entry(ur_param_entry_t *entry)
{
    if (g_param.storage == NULL || g_param.storage->save == NULL) {
        return UR_OK;
    }

    if (!(entry->def->flags & UR_PARAM_FLAG_PERSIST)) {
        return UR_OK;
    }

    size_t size = get_type_size(entry->def->type);
    if (entry->def->type == UR_PARAM_TYPE_STR ||
        entry->def->type == UR_PARAM_TYPE_BLOB) {
        size = entry->def->size;
    }

    ur_err_t err = g_param.storage->save(
        entry->def->name,
        (ur_param_type_t)entry->def->type,
        &entry->value,
        size
    );

    if (err == UR_OK) {
        entry->flags &= ~UR_PARAM_FLAG_DIRTY;
    }

    return err;
}

/* ============================================================================
 * Initialization
 * ========================================================================== */

ur_err_t ur_param_init(const ur_param_def_t *defs, size_t count,
                       const ur_param_storage_t *storage)
{
    if (defs == NULL || count == 0) {
        return UR_ERR_INVALID_ARG;
    }

    if (count > UR_CFG_PARAM_MAX_COUNT) {
        return UR_ERR_NO_MEMORY;
    }

    memset(&g_param, 0, sizeof(g_param));
    g_param.storage = storage;

    /* Initialize storage backend */
    if (storage != NULL && storage->init != NULL) {
        ur_err_t err = storage->init();
        if (err != UR_OK) {
            UR_LOGW("Param: storage init failed: %d", err);
        }
    }

    /* Register parameters with default values */
    for (size_t i = 0; i < count; i++) {
        g_param.entries[i].def = &defs[i];
        g_param.entries[i].value = defs[i].default_val;
        g_param.entries[i].flags = defs[i].flags & ~UR_PARAM_FLAG_DIRTY;
    }
    g_param.count = count;

    /* Load persisted values */
    ur_param_load_all();

    g_param.initialized = true;

    UR_LOGI("Param: initialized %d parameters", count);

#if UR_CFG_BUS_ENABLE
    /* Publish ready signal */
    ur_signal_t sig = { .id = SIG_PARAM_READY };
    ur_publish(sig);
#endif

    return UR_OK;
}

int ur_param_load_all(void)
{
    if (g_param.storage == NULL || g_param.storage->load == NULL) {
        return 0;
    }

    int loaded = 0;

    for (size_t i = 0; i < g_param.count; i++) {
        ur_param_entry_t *entry = &g_param.entries[i];

        if (!(entry->def->flags & UR_PARAM_FLAG_PERSIST)) {
            continue;
        }

        size_t size = get_type_size(entry->def->type);
        if (entry->def->type == UR_PARAM_TYPE_STR ||
            entry->def->type == UR_PARAM_TYPE_BLOB) {
            size = entry->def->size;
        }

        ur_param_value_t value;
        ur_err_t err = g_param.storage->load(
            entry->def->name,
            (ur_param_type_t)entry->def->type,
            &value,
            size
        );

        if (err == UR_OK) {
            entry->value = value;
            loaded++;
            UR_LOGD("Param: loaded '%s'", entry->def->name);
        }
    }

    return loaded;
}

int ur_param_save_all(void)
{
    if (g_param.storage == NULL) {
        return 0;
    }

    int saved = 0;

    for (size_t i = 0; i < g_param.count; i++) {
        ur_param_entry_t *entry = &g_param.entries[i];

        if (entry->flags & UR_PARAM_FLAG_DIRTY) {
            if (save_entry(entry) == UR_OK) {
                saved++;
            }
        }
    }

    if (g_param.storage->commit != NULL) {
        g_param.storage->commit();
    }

    return saved;
}

ur_err_t ur_param_reset_defaults(bool persist)
{
    for (size_t i = 0; i < g_param.count; i++) {
        ur_param_entry_t *entry = &g_param.entries[i];
        entry->value = entry->def->default_val;
        entry->flags |= UR_PARAM_FLAG_DIRTY;
    }

    if (persist) {
        if (g_param.storage != NULL && g_param.storage->erase != NULL) {
            g_param.storage->erase();
        }
        ur_param_save_all();
    }

    return UR_OK;
}

/* ============================================================================
 * Type-Safe Getters
 * ========================================================================== */

#define PARAM_GET_IMPL(suffix, ctype, member, param_type) \
ur_err_t ur_param_get_##suffix(uint16_t id, ctype *value) \
{ \
    if (value == NULL) return UR_ERR_INVALID_ARG; \
    ur_param_entry_t *entry = find_entry(id); \
    if (entry == NULL) return UR_ERR_NOT_FOUND; \
    if (entry->def->type != param_type) return UR_ERR_INVALID_ARG; \
    *value = entry->value.member; \
    return UR_OK; \
}

PARAM_GET_IMPL(u8, uint8_t, u8, UR_PARAM_TYPE_U8)
PARAM_GET_IMPL(u16, uint16_t, u16, UR_PARAM_TYPE_U16)
PARAM_GET_IMPL(u32, uint32_t, u32, UR_PARAM_TYPE_U32)
PARAM_GET_IMPL(i8, int8_t, i8, UR_PARAM_TYPE_I8)
PARAM_GET_IMPL(i16, int16_t, i16, UR_PARAM_TYPE_I16)
PARAM_GET_IMPL(i32, int32_t, i32, UR_PARAM_TYPE_I32)
PARAM_GET_IMPL(f32, float, f32, UR_PARAM_TYPE_F32)
PARAM_GET_IMPL(bool, bool, b, UR_PARAM_TYPE_BOOL)

ur_err_t ur_param_get_str(uint16_t id, char *buffer, size_t max_len)
{
    if (buffer == NULL || max_len == 0) {
        return UR_ERR_INVALID_ARG;
    }

    ur_param_entry_t *entry = find_entry(id);
    if (entry == NULL) {
        return UR_ERR_NOT_FOUND;
    }

    if (entry->def->type != UR_PARAM_TYPE_STR) {
        return UR_ERR_INVALID_ARG;
    }

    size_t copy_len = UR_MIN(max_len - 1, entry->def->size - 1);
    strncpy(buffer, entry->value.str, copy_len);
    buffer[copy_len] = '\0';

    return UR_OK;
}

ur_err_t ur_param_get_blob(uint16_t id, void *buffer, size_t *len)
{
    if (buffer == NULL || len == NULL || *len == 0) {
        return UR_ERR_INVALID_ARG;
    }

    ur_param_entry_t *entry = find_entry(id);
    if (entry == NULL) {
        return UR_ERR_NOT_FOUND;
    }

    if (entry->def->type != UR_PARAM_TYPE_BLOB) {
        return UR_ERR_INVALID_ARG;
    }

    size_t copy_len = UR_MIN(*len, entry->def->size);
    memcpy(buffer, entry->value.blob, copy_len);
    *len = copy_len;

    return UR_OK;
}

/* ============================================================================
 * Type-Safe Setters
 * ========================================================================== */

#define PARAM_SET_IMPL(suffix, ctype, member, param_type) \
ur_err_t ur_param_set_##suffix(uint16_t id, ctype value) \
{ \
    ur_param_entry_t *entry = find_entry(id); \
    if (entry == NULL) return UR_ERR_NOT_FOUND; \
    if (entry->def->type != param_type) return UR_ERR_INVALID_ARG; \
    if (entry->def->flags & UR_PARAM_FLAG_READONLY) return UR_ERR_INVALID_STATE; \
    if (entry->value.member == value) return UR_OK; \
    entry->value.member = value; \
    entry->flags |= UR_PARAM_FLAG_DIRTY; \
    if (!g_param.batch_mode && (entry->def->flags & UR_PARAM_FLAG_PERSIST)) { \
        save_entry(entry); \
    } \
    if (entry->def->flags & UR_PARAM_FLAG_NOTIFY) { \
        notify_change(id, &entry->value); \
    } \
    return UR_OK; \
}

PARAM_SET_IMPL(u8, uint8_t, u8, UR_PARAM_TYPE_U8)
PARAM_SET_IMPL(u16, uint16_t, u16, UR_PARAM_TYPE_U16)
PARAM_SET_IMPL(u32, uint32_t, u32, UR_PARAM_TYPE_U32)
PARAM_SET_IMPL(i8, int8_t, i8, UR_PARAM_TYPE_I8)
PARAM_SET_IMPL(i16, int16_t, i16, UR_PARAM_TYPE_I16)
PARAM_SET_IMPL(i32, int32_t, i32, UR_PARAM_TYPE_I32)
PARAM_SET_IMPL(f32, float, f32, UR_PARAM_TYPE_F32)
PARAM_SET_IMPL(bool, bool, b, UR_PARAM_TYPE_BOOL)

ur_err_t ur_param_set_str(uint16_t id, const char *value)
{
    if (value == NULL) {
        return UR_ERR_INVALID_ARG;
    }

    ur_param_entry_t *entry = find_entry(id);
    if (entry == NULL) {
        return UR_ERR_NOT_FOUND;
    }

    if (entry->def->type != UR_PARAM_TYPE_STR) {
        return UR_ERR_INVALID_ARG;
    }

    if (entry->def->flags & UR_PARAM_FLAG_READONLY) {
        return UR_ERR_INVALID_STATE;
    }

    /* Check if value changed */
    if (strncmp(entry->value.str, value, entry->def->size - 1) == 0) {
        return UR_OK;
    }

    strncpy(entry->value.str, value, entry->def->size - 1);
    entry->value.str[entry->def->size - 1] = '\0';
    entry->flags |= UR_PARAM_FLAG_DIRTY;

    if (!g_param.batch_mode && (entry->def->flags & UR_PARAM_FLAG_PERSIST)) {
        save_entry(entry);
    }

    if (entry->def->flags & UR_PARAM_FLAG_NOTIFY) {
        notify_change(id, &entry->value);
    }

    return UR_OK;
}

ur_err_t ur_param_set_blob(uint16_t id, const void *data, size_t len)
{
    if (data == NULL || len == 0) {
        return UR_ERR_INVALID_ARG;
    }

    ur_param_entry_t *entry = find_entry(id);
    if (entry == NULL) {
        return UR_ERR_NOT_FOUND;
    }

    if (entry->def->type != UR_PARAM_TYPE_BLOB) {
        return UR_ERR_INVALID_ARG;
    }

    if (entry->def->flags & UR_PARAM_FLAG_READONLY) {
        return UR_ERR_INVALID_STATE;
    }

    size_t copy_len = UR_MIN(len, entry->def->size);
    memcpy(entry->value.blob, data, copy_len);
    entry->flags |= UR_PARAM_FLAG_DIRTY;

    if (!g_param.batch_mode && (entry->def->flags & UR_PARAM_FLAG_PERSIST)) {
        save_entry(entry);
    }

    if (entry->def->flags & UR_PARAM_FLAG_NOTIFY) {
        notify_change(id, &entry->value);
    }

    return UR_OK;
}

/* ============================================================================
 * Batch Operations
 * ========================================================================== */

void ur_param_batch_begin(void)
{
    g_param.batch_mode = true;
}

int ur_param_commit(void)
{
    g_param.batch_mode = false;
    return ur_param_save_all();
}

void ur_param_batch_abort(void)
{
    g_param.batch_mode = false;

    /* Reload from storage to discard changes */
    for (size_t i = 0; i < g_param.count; i++) {
        if (g_param.entries[i].flags & UR_PARAM_FLAG_DIRTY) {
            g_param.entries[i].value = g_param.entries[i].def->default_val;
            g_param.entries[i].flags &= ~UR_PARAM_FLAG_DIRTY;
        }
    }

    ur_param_load_all();
}

/* ============================================================================
 * Query Functions
 * ========================================================================== */

const ur_param_def_t *ur_param_get_def(uint16_t id)
{
    ur_param_entry_t *entry = find_entry(id);
    return (entry != NULL) ? entry->def : NULL;
}

size_t ur_param_count(void)
{
    return g_param.count;
}

bool ur_param_exists(uint16_t id)
{
    return find_entry(id) != NULL;
}

bool ur_param_is_dirty(uint16_t id)
{
    ur_param_entry_t *entry = find_entry(id);
    return (entry != NULL) && (entry->flags & UR_PARAM_FLAG_DIRTY);
}

/* ============================================================================
 * Debug
 * ========================================================================== */

void ur_param_dump(void)
{
#if UR_CFG_ENABLE_LOGGING
    UR_LOGI("=== Parameters (%d) ===", g_param.count);

    for (size_t i = 0; i < g_param.count; i++) {
        const ur_param_entry_t *entry = &g_param.entries[i];
        const ur_param_def_t *def = entry->def;

        char flags[8] = "";
        if (def->flags & UR_PARAM_FLAG_PERSIST) strcat(flags, "P");
        if (def->flags & UR_PARAM_FLAG_READONLY) strcat(flags, "R");
        if (entry->flags & UR_PARAM_FLAG_DIRTY) strcat(flags, "D");

        switch (def->type) {
            case UR_PARAM_TYPE_U8:
                UR_LOGI("  [%d] %s = %u [%s]", def->id, def->name, entry->value.u8, flags);
                break;
            case UR_PARAM_TYPE_U16:
                UR_LOGI("  [%d] %s = %u [%s]", def->id, def->name, entry->value.u16, flags);
                break;
            case UR_PARAM_TYPE_U32:
                UR_LOGI("  [%d] %s = %lu [%s]", def->id, def->name, entry->value.u32, flags);
                break;
            case UR_PARAM_TYPE_STR:
                UR_LOGI("  [%d] %s = \"%s\" [%s]", def->id, def->name, entry->value.str, flags);
                break;
            default:
                UR_LOGI("  [%d] %s = <type %d> [%s]", def->id, def->name, def->type, flags);
                break;
        }
    }
#endif
}

/* ============================================================================
 * RAM-Only Storage Backend
 * ========================================================================== */

static ur_err_t ram_init(void) { return UR_OK; }
static ur_err_t ram_load(const char *key, ur_param_type_t type,
                         ur_param_value_t *value, size_t size)
{
    (void)key; (void)type; (void)value; (void)size;
    return UR_ERR_NOT_FOUND;
}
static ur_err_t ram_save(const char *key, ur_param_type_t type,
                         const ur_param_value_t *value, size_t size)
{
    (void)key; (void)type; (void)value; (void)size;
    return UR_OK;
}
static ur_err_t ram_commit(void) { return UR_OK; }
static ur_err_t ram_erase(void) { return UR_OK; }

const ur_param_storage_t ur_param_storage_ram = {
    .init = ram_init,
    .load = ram_load,
    .save = ram_save,
    .commit = ram_commit,
    .erase = ram_erase,
};

/* ============================================================================
 * NVS Storage Backend (ESP-IDF)
 * ========================================================================== */

#if defined(CONFIG_IDF_TARGET) || defined(ESP_PLATFORM)
#include "nvs_flash.h"
#include "nvs.h"

static nvs_handle_t g_nvs_handle = 0;

static ur_err_t nvs_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        return UR_ERR_INVALID_STATE;
    }

    err = nvs_open(UR_CFG_PARAM_NAMESPACE, NVS_READWRITE, &g_nvs_handle);
    return (err == ESP_OK) ? UR_OK : UR_ERR_INVALID_STATE;
}

static ur_err_t nvs_storage_load(const char *key, ur_param_type_t type,
                                  ur_param_value_t *value, size_t size)
{
    esp_err_t err;

    switch (type) {
        case UR_PARAM_TYPE_U8:
        case UR_PARAM_TYPE_BOOL:
            err = nvs_get_u8(g_nvs_handle, key, &value->u8);
            break;
        case UR_PARAM_TYPE_U16:
            err = nvs_get_u16(g_nvs_handle, key, &value->u16);
            break;
        case UR_PARAM_TYPE_U32:
            err = nvs_get_u32(g_nvs_handle, key, &value->u32);
            break;
        case UR_PARAM_TYPE_I8:
            err = nvs_get_i8(g_nvs_handle, key, &value->i8);
            break;
        case UR_PARAM_TYPE_I16:
            err = nvs_get_i16(g_nvs_handle, key, &value->i16);
            break;
        case UR_PARAM_TYPE_I32:
            err = nvs_get_i32(g_nvs_handle, key, &value->i32);
            break;
        case UR_PARAM_TYPE_STR:
            err = nvs_get_str(g_nvs_handle, key, value->str, &size);
            break;
        case UR_PARAM_TYPE_BLOB:
        case UR_PARAM_TYPE_F32:
            err = nvs_get_blob(g_nvs_handle, key, value, &size);
            break;
        default:
            return UR_ERR_INVALID_ARG;
    }

    return (err == ESP_OK) ? UR_OK : UR_ERR_NOT_FOUND;
}

static ur_err_t nvs_storage_save(const char *key, ur_param_type_t type,
                                  const ur_param_value_t *value, size_t size)
{
    esp_err_t err;

    switch (type) {
        case UR_PARAM_TYPE_U8:
        case UR_PARAM_TYPE_BOOL:
            err = nvs_set_u8(g_nvs_handle, key, value->u8);
            break;
        case UR_PARAM_TYPE_U16:
            err = nvs_set_u16(g_nvs_handle, key, value->u16);
            break;
        case UR_PARAM_TYPE_U32:
            err = nvs_set_u32(g_nvs_handle, key, value->u32);
            break;
        case UR_PARAM_TYPE_I8:
            err = nvs_set_i8(g_nvs_handle, key, value->i8);
            break;
        case UR_PARAM_TYPE_I16:
            err = nvs_set_i16(g_nvs_handle, key, value->i16);
            break;
        case UR_PARAM_TYPE_I32:
            err = nvs_set_i32(g_nvs_handle, key, value->i32);
            break;
        case UR_PARAM_TYPE_STR:
            err = nvs_set_str(g_nvs_handle, key, value->str);
            break;
        case UR_PARAM_TYPE_BLOB:
        case UR_PARAM_TYPE_F32:
            err = nvs_set_blob(g_nvs_handle, key, value, size);
            break;
        default:
            return UR_ERR_INVALID_ARG;
    }

    return (err == ESP_OK) ? UR_OK : UR_ERR_NO_MEMORY;
}

static ur_err_t nvs_storage_commit(void)
{
    return (nvs_commit(g_nvs_handle) == ESP_OK) ? UR_OK : UR_ERR_INVALID_STATE;
}

static ur_err_t nvs_storage_erase(void)
{
    return (nvs_erase_all(g_nvs_handle) == ESP_OK) ? UR_OK : UR_ERR_INVALID_STATE;
}

const ur_param_storage_t ur_param_storage_nvs = {
    .init = nvs_storage_init,
    .load = nvs_storage_load,
    .save = nvs_storage_save,
    .commit = nvs_storage_commit,
    .erase = nvs_storage_erase,
};

#endif /* ESP_PLATFORM */

#endif /* UR_CFG_PARAM_ENABLE */
