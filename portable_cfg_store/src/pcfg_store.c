#include "pcfg_store.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "pcfg_media_internal.h"

static bool pcfg_size_add_overflows_u32(uint32_t base, size_t size)
{
    return (size > UINT32_MAX) || (base > (UINT32_MAX - (uint32_t)size));
}

static bool pcfg_program_unit_valid(size_t unit)
{
    return (unit == 1u) || (unit == 2u) || (unit == 4u) ||
           (unit == 8u) || (unit == 16u);
}

static bool pcfg_ranges_overlap(uint32_t first, size_t first_size,
                                uint32_t second, size_t second_size)
{
    uint64_t first_end = (uint64_t)first + first_size;
    uint64_t second_end = (uint64_t)second + second_size;
    return ((uint64_t)first < second_end) && ((uint64_t)second < first_end);
}

static bool pcfg_geometry_valid(const pcfg_config_t *config,
                                const void *scratch, size_t scratch_size)
{
    size_t minimum_slot = PCFG_MEDIA_HEADER_SIZE + 1u + PCFG_MEDIA_COMMIT_SIZE;
    uintptr_t scratch_address = (uintptr_t)scratch;

    if ((config->slot_size < minimum_slot) || (config->erase_unit == 0u) ||
        !pcfg_program_unit_valid(config->program_unit) ||
        ((config->erase_unit % config->program_unit) != 0u) ||
        ((config->slot_size % config->erase_unit) != 0u) ||
        ((config->slot_address[0] % config->erase_unit) != 0u) ||
        ((config->slot_address[1] % config->erase_unit) != 0u) ||
        (config->io_chunk_size < config->program_unit) ||
        (config->io_chunk_size > scratch_size) ||
        (scratch_size < config->program_unit) ||
        ((scratch_address % config->program_unit) != 0u) ||
        ((PCFG_MEDIA_HEADER_SIZE % config->program_unit) != 0u) ||
        ((PCFG_MEDIA_COMMIT_SIZE % config->program_unit) != 0u))
    {
        return false;
    }

    if ((config->program_page_size != 0u) &&
        (((config->program_page_size % config->program_unit) != 0u) ||
         (config->program_page_size < config->program_unit)))
    {
        return false;
    }

    if (pcfg_size_add_overflows_u32(config->slot_address[0],
                                    config->slot_size) ||
        pcfg_size_add_overflows_u32(config->slot_address[1],
                                    config->slot_size) ||
        pcfg_ranges_overlap(config->slot_address[0], config->slot_size,
                            config->slot_address[1], config->slot_size))
    {
        return false;
    }

    return true;
}

pcfg_status_t pcfg_init(pcfg_store_t *store,
                        const pcfg_config_t *config,
                        const pcfg_storage_ops_t *storage,
                        void *storage_context,
                        void *scratch,
                        size_t scratch_size)
{
    if ((store == NULL) || (config == NULL) || (storage == NULL) ||
        (scratch == NULL) || (storage->read == NULL) ||
        (storage->program == NULL) || (storage->erase == NULL))
    {
        return PCFG_INVALID_ARGUMENT;
    }

    if (!pcfg_geometry_valid(config, scratch, scratch_size))
    {
        return PCFG_UNSUPPORTED_GEOMETRY;
    }

    memset(store, 0, sizeof(*store));
    store->config = *config;
    store->storage = *storage;
    store->storage_context = storage_context;
    store->scratch = (uint8_t *)scratch;
    store->scratch_size = scratch_size;
    store->active_slot = -1;
    return PCFG_OK;
}

pcfg_status_t pcfg_load(pcfg_store_t *store,
                        uint8_t *payload,
                        size_t capacity,
                        size_t *payload_size,
                        pcfg_validate_fn validate,
                        void *validate_context)
{
    (void)store;
    (void)payload;
    (void)capacity;
    (void)validate;
    (void)validate_context;
    if (payload_size != NULL)
    {
        *payload_size = 0u;
    }
    return PCFG_NOT_READY;
}

pcfg_status_t pcfg_save(pcfg_store_t *store,
                        const uint8_t *payload,
                        size_t payload_size,
                        pcfg_validate_fn validate,
                        void *validate_context)
{
    (void)store;
    (void)payload;
    (void)payload_size;
    (void)validate;
    (void)validate_context;
    return PCFG_NOT_READY;
}

pcfg_status_t pcfg_factory_reset(pcfg_store_t *store,
                                 const uint8_t *default_payload,
                                 size_t default_size,
                                 pcfg_validate_fn validate,
                                 void *validate_context)
{
    return pcfg_save(store, default_payload, default_size, validate,
                     validate_context);
}

int pcfg_active_slot(const pcfg_store_t *store)
{
    return (store != NULL) ? store->active_slot : -1;
}

uint32_t pcfg_active_sequence(const pcfg_store_t *store)
{
    return (store != NULL) ? store->active_sequence : 0u;
}

bool pcfg_is_read_only(const pcfg_store_t *store)
{
    return (store != NULL) && store->read_only;
}
