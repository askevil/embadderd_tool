#ifndef PCFG_CJSON_H
#define PCFG_CJSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cJSON.h>

#include "pcfg_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef pcfg_status_t (*pcfg_cjson_validate_fn)(void *ctx,
                                                 const cJSON *root);

typedef struct
{
    void *root;
    const uint8_t *default_json;
    size_t default_size;
    pcfg_cjson_validate_fn validator;
    void *validator_context;
    bool dirty;
} pcfg_cjson_document_t;

pcfg_status_t pcfg_cjson_init(pcfg_cjson_document_t *document,
                              const uint8_t *default_json,
                              size_t default_size,
                              pcfg_cjson_validate_fn validator,
                              void *validator_context);
void pcfg_cjson_destroy(pcfg_cjson_document_t *document);

pcfg_status_t pcfg_cjson_load_bytes(pcfg_cjson_document_t *document,
                                    const uint8_t *json,
                                    size_t json_size,
                                    pcfg_cjson_validate_fn validator,
                                    void *validator_context);
pcfg_status_t pcfg_cjson_load(pcfg_cjson_document_t *document,
                              pcfg_store_t *store,
                              uint8_t *buffer,
                              size_t capacity);
pcfg_status_t pcfg_cjson_load_default(pcfg_cjson_document_t *document);

pcfg_status_t pcfg_cjson_serialize(const pcfg_cjson_document_t *document,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *required_size);
pcfg_status_t pcfg_cjson_save(pcfg_cjson_document_t *document,
                              pcfg_store_t *store,
                              char *buffer,
                              size_t capacity);
pcfg_status_t pcfg_cjson_factory_reset(pcfg_cjson_document_t *document,
                                       pcfg_store_t *store);

pcfg_status_t pcfg_cjson_get_string(const pcfg_cjson_document_t *document,
                                    const char *path,
                                    char *value,
                                    size_t capacity);
pcfg_status_t pcfg_cjson_get_int32(const pcfg_cjson_document_t *document,
                                   const char *path,
                                   int32_t *value);
pcfg_status_t pcfg_cjson_get_bool(const pcfg_cjson_document_t *document,
                                  const char *path,
                                  bool *value);
pcfg_status_t pcfg_cjson_get_double(const pcfg_cjson_document_t *document,
                                    const char *path,
                                    double *value);

pcfg_status_t pcfg_cjson_set_string(pcfg_cjson_document_t *document,
                                    const char *path,
                                    const char *value);
pcfg_status_t pcfg_cjson_set_int32(pcfg_cjson_document_t *document,
                                   const char *path,
                                   int32_t value);
pcfg_status_t pcfg_cjson_set_bool(pcfg_cjson_document_t *document,
                                  const char *path,
                                  bool value);
pcfg_status_t pcfg_cjson_set_double(pcfg_cjson_document_t *document,
                                    const char *path,
                                    double value);

bool pcfg_cjson_is_dirty(const pcfg_cjson_document_t *document);
pcfg_status_t pcfg_cjson_clear_dirty(pcfg_cjson_document_t *document);

#ifdef __cplusplus
}
#endif

#endif
