#ifndef PCFG_STORE_H
#define PCFG_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    PCFG_OK = 0,
    PCFG_NOT_FOUND,
    PCFG_NOT_READY,
    PCFG_INVALID_ARGUMENT,
    PCFG_BUFFER_TOO_SMALL,
    PCFG_PAYLOAD_TOO_LARGE,
    PCFG_STORAGE_ERROR,
    PCFG_CORRUPT,
    PCFG_UNSUPPORTED_VERSION,
    PCFG_VALIDATION_FAILED,
    PCFG_READ_ONLY,
    PCFG_UNSUPPORTED_GEOMETRY,
    PCFG_SCRATCH_TOO_SMALL,
    PCFG_SEQUENCE_AMBIGUOUS
} pcfg_status_t;

typedef pcfg_status_t (*pcfg_read_fn)(void *ctx, uint32_t address,
                                      void *dst, size_t size);
typedef pcfg_status_t (*pcfg_program_fn)(void *ctx, uint32_t address,
                                         const void *src, size_t size);
typedef pcfg_status_t (*pcfg_erase_fn)(void *ctx, uint32_t address,
                                       size_t size);
typedef pcfg_status_t (*pcfg_sync_fn)(void *ctx);

typedef struct
{
    pcfg_read_fn read;
    pcfg_program_fn program;
    pcfg_erase_fn erase;
    pcfg_sync_fn sync;
} pcfg_storage_ops_t;

typedef struct
{
    uint32_t slot_address[2];
    size_t slot_size;
    size_t erase_unit;
    size_t program_unit;
    size_t program_page_size;
    size_t io_chunk_size;
    uint32_t format_magic;
} pcfg_config_t;

typedef pcfg_status_t (*pcfg_validate_fn)(void *ctx,
                                          const uint8_t *payload,
                                          size_t payload_size);

typedef struct
{
    pcfg_config_t config;
    pcfg_storage_ops_t storage;
    void *storage_context;
    uint8_t *scratch;
    size_t scratch_size;
    int active_slot;
    uint32_t active_sequence;
    bool scan_complete;
    bool read_only;
    bool reload_required;
} pcfg_store_t;

pcfg_status_t pcfg_init(pcfg_store_t *store,
                        const pcfg_config_t *config,
                        const pcfg_storage_ops_t *storage,
                        void *storage_context,
                        void *scratch,
                        size_t scratch_size);

/* Output and save input must not overlap scratch. Failed loads preserve output.
 * Load/semantic recovery requires scratch >= candidate payload length.
 * Save/readback without a semantic validator streams through io_chunk_size.
 * Hold one external transaction lock for every complete API call. */
pcfg_status_t pcfg_load(pcfg_store_t *store,
                        uint8_t *payload,
                        size_t capacity,
                        size_t *payload_size,
                        pcfg_validate_fn validate,
                        void *validate_context);

pcfg_status_t pcfg_save(pcfg_store_t *store,
                        const uint8_t *payload,
                        size_t payload_size,
                        pcfg_validate_fn validate,
                        void *validate_context);

pcfg_status_t pcfg_factory_reset(pcfg_store_t *store,
                                 const uint8_t *default_payload,
                                 size_t default_size,
                                 pcfg_validate_fn validate,
                                 void *validate_context);

int pcfg_active_slot(const pcfg_store_t *store);
uint32_t pcfg_active_sequence(const pcfg_store_t *store);
bool pcfg_is_read_only(const pcfg_store_t *store);
bool pcfg_reload_required(const pcfg_store_t *store);

#ifdef __cplusplus
}
#endif

#endif
