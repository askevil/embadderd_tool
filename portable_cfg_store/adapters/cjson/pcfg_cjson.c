#include "pcfg_cjson.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static cJSON *pcfg_root(const pcfg_cjson_document_t *document)
{
    return (document != NULL) ? (cJSON *)document->root : NULL;
}

static bool pcfg_key_has_dot(const char *key)
{
    return (key != NULL) && (strchr(key, '.') != NULL);
}

static pcfg_status_t pcfg_validate_tree(cJSON *root,
                                        pcfg_cjson_validate_fn validator,
                                        void *context)
{
    cJSON *item;
    cJSON *other;
    if ((root == NULL) || !cJSON_IsObject(root))
    {
        return PCFG_VALIDATION_FAILED;
    }
    cJSON_ArrayForEach(item, root)
    {
        if (pcfg_key_has_dot(item->string) || cJSON_IsArray(item))
        {
            return PCFG_VALIDATION_FAILED;
        }
        for (other = item->next; other != NULL; other = other->next)
        {
            if ((item->string != NULL) && (other->string != NULL) &&
                (strcmp(item->string, other->string) == 0))
            {
                return PCFG_VALIDATION_FAILED;
            }
        }
        if (cJSON_IsObject(item))
        {
            pcfg_status_t result = pcfg_validate_tree(item, NULL, NULL);
            if (result != PCFG_OK)
            {
                return result;
            }
        }
    }
    return (validator != NULL) ? validator(context, root) : PCFG_OK;
}

static cJSON *pcfg_parse_and_validate(const uint8_t *json, size_t json_size,
                                      pcfg_cjson_validate_fn validator,
                                      void *context,
                                      pcfg_status_t *status)
{
    char *text;
    cJSON *root;
    if ((json == NULL) || (json_size == 0u) || (json_size > SIZE_MAX - 1u))
    {
        *status = PCFG_INVALID_ARGUMENT;
        return NULL;
    }
    if (memchr(json, 0, json_size) != NULL)
    {
        *status = PCFG_CORRUPT;
        return NULL;
    }
    text = (char *)cJSON_malloc(json_size + 1u);
    if (text == NULL)
    {
        *status = PCFG_STORAGE_ERROR;
        return NULL;
    }
    memcpy(text, json, json_size);
    text[json_size] = '\0';
    root = cJSON_ParseWithLengthOpts(text, json_size + 1u, NULL, 1);
    cJSON_free(text);
    if (root == NULL)
    {
        *status = PCFG_CORRUPT;
        return NULL;
    }
    *status = pcfg_validate_tree(root, validator, context);
    if (*status != PCFG_OK)
    {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static cJSON *pcfg_find_path(const cJSON *root, const char *path)
{
    const char *cursor = path;
    char key[64];
    const char *dot;
    cJSON *current = (cJSON *)root;
    size_t length;

    if ((root == NULL) || (path == NULL) || (path[0] == '\0'))
    {
        return NULL;
    }
    while (current != NULL)
    {
        dot = strchr(cursor, '.');
        length = (dot != NULL) ? (size_t)(dot - cursor) : strlen(cursor);
        if ((length == 0u) || (length >= sizeof(key)))
        {
            return NULL;
        }
        memcpy(key, cursor, length);
        key[length] = '\0';
        current = cJSON_GetObjectItemCaseSensitive(current, key);
        if (dot == NULL)
        {
            return current;
        }
        if ((current == NULL) || !cJSON_IsObject(current))
        {
            return NULL;
        }
        cursor = dot + 1;
    }
    return NULL;
}

static bool pcfg_same_scalar_type(const cJSON *first, const cJSON *second)
{
    return (cJSON_IsNumber(first) && cJSON_IsNumber(second)) ||
           (cJSON_IsString(first) && cJSON_IsString(second)) ||
           (cJSON_IsBool(first) && cJSON_IsBool(second));
}

/* Edit a private copy: any path/type/allocation failure preserves the tree. */
static pcfg_status_t pcfg_set_item(pcfg_cjson_document_t *document,
                                   const char *path, cJSON *replacement)
{
    cJSON *copy;
    cJSON *parent;
    const char *cursor = path;
    pcfg_status_t status = PCFG_OK;
    if (replacement == NULL)
    {
        return PCFG_STORAGE_ERROR;
    }
    if ((document == NULL) || (pcfg_root(document) == NULL) ||
        (path == NULL) || (path[0] == '\0'))
    {
        cJSON_Delete(replacement);
        return PCFG_INVALID_ARGUMENT;
    }
    copy = cJSON_Duplicate(pcfg_root(document), 1);
    if (copy == NULL)
    {
        cJSON_Delete(replacement);
        return PCFG_STORAGE_ERROR;
    }
    parent = copy;
    for (;;)
    {
        const char *dot = strchr(cursor, '.');
        size_t length = (dot != NULL) ? (size_t)(dot - cursor) : strlen(cursor);
        char key[64];
        cJSON *existing;
        if ((length == 0u) || (length >= sizeof(key)))
        {
            status = PCFG_INVALID_ARGUMENT;
            break;
        }
        memcpy(key, cursor, length);
        key[length] = '\0';
        existing = cJSON_GetObjectItemCaseSensitive(parent, key);
        if (dot == NULL)
        {
            if ((existing != NULL) && !pcfg_same_scalar_type(existing, replacement))
            {
                status = PCFG_VALIDATION_FAILED;
                break;
            }
            if (existing != NULL)
            {
                if (!cJSON_ReplaceItemInObjectCaseSensitive(parent, key, replacement))
                {
                    status = PCFG_STORAGE_ERROR;
                    break;
                }
            }
            else if (!cJSON_AddItemToObject(parent, key, replacement))
            {
                status = PCFG_STORAGE_ERROR;
                break;
            }
            replacement = NULL; /* tree now owns it */
            break;
        }
        if (existing == NULL)
        {
            existing = cJSON_CreateObject();
            if ((existing == NULL) || !cJSON_AddItemToObject(parent, key, existing))
            {
                cJSON_Delete(existing);
                status = PCFG_STORAGE_ERROR;
                break;
            }
        }
        if (!cJSON_IsObject(existing))
        {
            status = PCFG_VALIDATION_FAILED;
            break;
        }
        parent = existing;
        cursor = dot + 1;
    }
    cJSON_Delete(replacement);
    if (status == PCFG_OK)
    {
        cJSON_Delete(pcfg_root(document));
        document->root = copy;
        document->dirty = true;
    }
    else
    {
        cJSON_Delete(copy);
    }
    return status;
}

pcfg_status_t pcfg_cjson_init(pcfg_cjson_document_t *document,
                              const uint8_t *default_json,
                              size_t default_size,
                              pcfg_cjson_validate_fn validator,
                              void *validator_context)
{
    if (document == NULL)
    {
        return PCFG_INVALID_ARGUMENT;
    }
    memset(document, 0, sizeof(*document));
    document->default_json = default_json;
    document->default_size = default_size;
    document->validator = validator;
    document->validator_context = validator_context;
    if ((default_json == NULL) != (default_size == 0u))
    {
        return PCFG_INVALID_ARGUMENT;
    }
    if (default_json != NULL)
    {
        pcfg_status_t status;
        cJSON *parsed = pcfg_parse_and_validate(default_json, default_size,
                                                 validator, validator_context,
                                                 &status);
        cJSON_Delete(parsed);
        return status;
    }
    return PCFG_OK;
}

void pcfg_cjson_destroy(pcfg_cjson_document_t *document)
{
    if (document != NULL)
    {
        cJSON_Delete(pcfg_root(document));
        memset(document, 0, sizeof(*document));
    }
}

pcfg_status_t pcfg_cjson_load_bytes(pcfg_cjson_document_t *document,
                                    const uint8_t *json,
                                    size_t json_size,
                                    pcfg_cjson_validate_fn validator,
                                    void *validator_context)
{
    cJSON *root;
    pcfg_status_t status;
    if (document == NULL)
    {
        return PCFG_INVALID_ARGUMENT;
    }
    if (validator == NULL)
    {
        validator = document->validator;
        validator_context = document->validator_context;
    }
    root = pcfg_parse_and_validate(json, json_size, validator,
                                   validator_context, &status);
    if (root == NULL)
    {
        return status;
    }
    cJSON_Delete(pcfg_root(document));
    document->root = root;
    document->dirty = false;
    return PCFG_OK;
}

pcfg_status_t pcfg_cjson_load_default(pcfg_cjson_document_t *document)
{
    if ((document == NULL) || (document->default_json == NULL))
    {
        return PCFG_INVALID_ARGUMENT;
    }
    return pcfg_cjson_load_bytes(document, document->default_json,
                                 document->default_size, NULL, NULL);
}

static pcfg_status_t pcfg_validate_bytes(void *context, const uint8_t *bytes,
                                         size_t size)
{
    pcfg_cjson_document_t *document = (pcfg_cjson_document_t *)context;
    pcfg_status_t status;
    cJSON *root = pcfg_parse_and_validate(bytes, size, document->validator,
                                          document->validator_context, &status);
    cJSON_Delete(root);
    return status;
}

pcfg_status_t pcfg_cjson_load(pcfg_cjson_document_t *document,
                              pcfg_store_t *store,
                              uint8_t *buffer,
                              size_t capacity)
{
    size_t size = 0u;
    pcfg_status_t status;
    if ((document == NULL) || (store == NULL) || (buffer == NULL))
    {
        return PCFG_INVALID_ARGUMENT;
    }
    status = pcfg_load(store, buffer, capacity, &size, pcfg_validate_bytes, document);
    if (status == PCFG_NOT_FOUND)
    {
        return pcfg_cjson_load_default(document);
    }
    if (status != PCFG_OK)
    {
        return status;
    }
    return pcfg_cjson_load_bytes(document, buffer, size, NULL, NULL);
}

pcfg_status_t pcfg_cjson_serialize(const pcfg_cjson_document_t *document,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *required_size)
{
    char *printed;
    size_t length;
    if ((document == NULL) || (pcfg_root(document) == NULL) ||
        (required_size == NULL))
    {
        return PCFG_INVALID_ARGUMENT;
    }
    printed = cJSON_PrintUnformatted(pcfg_root(document));
    if (printed == NULL)
    {
        return PCFG_STORAGE_ERROR;
    }
    length = strlen(printed);
    *required_size = length;
    if ((buffer == NULL) || (capacity < length + 1u))
    {
        cJSON_free(printed);
        return PCFG_BUFFER_TOO_SMALL;
    }
    memcpy(buffer, printed, length + 1u);
    cJSON_free(printed);
    return PCFG_OK;
}

pcfg_status_t pcfg_cjson_save(pcfg_cjson_document_t *document,
                              pcfg_store_t *store,
                              char *buffer,
                              size_t capacity)
{
    size_t required;
    pcfg_status_t status = pcfg_cjson_serialize(document, buffer, capacity,
                                                &required);
    if (status != PCFG_OK)
    {
        return status;
    }
    status = pcfg_save(store, (const uint8_t *)buffer, required, pcfg_validate_bytes, document);
    if (status == PCFG_OK)
    {
        document->dirty = false;
    }
    return status;
}

pcfg_status_t pcfg_cjson_factory_reset(pcfg_cjson_document_t *document,
                                       pcfg_store_t *store)
{
    cJSON *defaults;
    pcfg_status_t status;
    if ((document == NULL) || (store == NULL) ||
        (document->default_json == NULL))
    {
        return PCFG_INVALID_ARGUMENT;
    }
    defaults = pcfg_parse_and_validate(document->default_json,
                                       document->default_size,
                                       document->validator,
                                       document->validator_context, &status);
    if (defaults == NULL)
    {
        return status;
    }
    status = pcfg_factory_reset(store, document->default_json,
                                 document->default_size, pcfg_validate_bytes,
                                 document);
    if (status == PCFG_OK)
    {
        cJSON_Delete(pcfg_root(document));
        document->root = defaults;
        document->dirty = false;
    }
    else
    {
        cJSON_Delete(defaults);
    }
    return status;
}

pcfg_status_t pcfg_cjson_get_string(const pcfg_cjson_document_t *document,
                                    const char *path, char *value,
                                    size_t capacity)
{
    cJSON *item = pcfg_find_path(pcfg_root(document), path);
    size_t length;
    if ((item == NULL) || (value == NULL) || !cJSON_IsString(item))
    {
        return PCFG_VALIDATION_FAILED;
    }
    length = strlen(item->valuestring);
    if (capacity <= length)
    {
        return PCFG_BUFFER_TOO_SMALL;
    }
    memcpy(value, item->valuestring, length + 1u);
    return PCFG_OK;
}

pcfg_status_t pcfg_cjson_get_int32(const pcfg_cjson_document_t *document,
                                   const char *path, int32_t *value)
{
    cJSON *item = pcfg_find_path(pcfg_root(document), path);
    double number;
    if ((item == NULL) || (value == NULL) || !cJSON_IsNumber(item))
    {
        return PCFG_VALIDATION_FAILED;
    }
    number = item->valuedouble;
    if (!isfinite(number) || floor(number) != number ||
        number < (double)INT32_MIN || number > (double)INT32_MAX)
    {
        return PCFG_VALIDATION_FAILED;
    }
    *value = (int32_t)number;
    return PCFG_OK;
}

pcfg_status_t pcfg_cjson_get_bool(const pcfg_cjson_document_t *document,
                                  const char *path, bool *value)
{
    cJSON *item = pcfg_find_path(pcfg_root(document), path);
    if ((item == NULL) || (value == NULL) || !cJSON_IsBool(item))
    {
        return PCFG_VALIDATION_FAILED;
    }
    *value = cJSON_IsTrue(item);
    return PCFG_OK;
}

pcfg_status_t pcfg_cjson_get_double(const pcfg_cjson_document_t *document,
                                    const char *path, double *value)
{
    cJSON *item = pcfg_find_path(pcfg_root(document), path);
    if ((item == NULL) || (value == NULL) || !cJSON_IsNumber(item) ||
        !isfinite(item->valuedouble))
    {
        return PCFG_VALIDATION_FAILED;
    }
    *value = item->valuedouble;
    return PCFG_OK;
}

pcfg_status_t pcfg_cjson_set_string(pcfg_cjson_document_t *document,
                                    const char *path, const char *value)
{
    if ((document == NULL) || (value == NULL))
    {
        return PCFG_INVALID_ARGUMENT;
    }
    return pcfg_set_item(document, path, cJSON_CreateString(value));
}

pcfg_status_t pcfg_cjson_set_int32(pcfg_cjson_document_t *document,
                                   const char *path, int32_t value)
{
    if (document == NULL)
    {
        return PCFG_INVALID_ARGUMENT;
    }
    return pcfg_set_item(document, path, cJSON_CreateNumber(value));
}

pcfg_status_t pcfg_cjson_set_bool(pcfg_cjson_document_t *document,
                                  const char *path, bool value)
{
    if (document == NULL)
    {
        return PCFG_INVALID_ARGUMENT;
    }
    return pcfg_set_item(document, path, cJSON_CreateBool(value));
}

pcfg_status_t pcfg_cjson_set_double(pcfg_cjson_document_t *document,
                                    const char *path, double value)
{
    if ((document == NULL) || !isfinite(value))
    {
        return PCFG_VALIDATION_FAILED;
    }
    return pcfg_set_item(document, path, cJSON_CreateNumber(value));
}

bool pcfg_cjson_is_dirty(const pcfg_cjson_document_t *document)
{
    return (document != NULL) && document->dirty;
}

pcfg_status_t pcfg_cjson_clear_dirty(pcfg_cjson_document_t *document)
{
    if (document == NULL)
    {
        return PCFG_INVALID_ARGUMENT;
    }
    document->dirty = false;
    return PCFG_OK;
}
