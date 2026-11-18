#include "pcfg_store.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "pcfg_media_internal.h"

typedef enum
{
    PCFG_SLOT_BLANK = 0,
    PCFG_SLOT_VALID_METADATA,
    PCFG_SLOT_CORRUPT,
    PCFG_SLOT_UNSUPPORTED,
    PCFG_SLOT_STORAGE_ERROR
} pcfg_slot_kind_t;

typedef struct
{
    pcfg_slot_kind_t kind;
    pcfg_media_header_t header;
    pcfg_media_commit_t commit;
} pcfg_slot_info_t;

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

    /* A/B slots must not share an erase unit, even when their byte ranges do not overlap. */
    if ((config->slot_address[0] / config->erase_unit) ==
        (config->slot_address[1] / config->erase_unit))
    {
        return false;
    }

    return true;
}

static bool pcfg_all_ff(const uint8_t *bytes, size_t size)
{
    size_t index;
    for (index = 0u; index < size; ++index)
    {
        if (bytes[index] != 0xffu)
        {
            return false;
        }
    }
    return true;
}

static uint32_t pcfg_slot_address(const pcfg_store_t *store, int slot,
                                  size_t offset)
{
    return store->config.slot_address[slot] + (uint32_t)offset;
}

static pcfg_slot_kind_t pcfg_inspect_slot(const pcfg_store_t *store, int slot,
                                          pcfg_slot_info_t *info)
{
    uint8_t header_bytes[PCFG_MEDIA_HEADER_SIZE];
    uint8_t commit_bytes[PCFG_MEDIA_COMMIT_SIZE];
    pcfg_media_result_t media_result;
    uint32_t payload_limit = (uint32_t)(store->config.slot_size -
                                        PCFG_MEDIA_HEADER_SIZE -
                                        PCFG_MEDIA_COMMIT_SIZE);

    memset(info, 0, sizeof(*info));
    if (store->storage.read(store->storage_context,
                            pcfg_slot_address(store, slot, 0u),
                            header_bytes, sizeof(header_bytes)) != PCFG_OK)
    {
        info->kind = PCFG_SLOT_STORAGE_ERROR;
        return info->kind;
    }
    if (pcfg_all_ff(header_bytes, sizeof(header_bytes)))
    {
        info->kind = PCFG_SLOT_BLANK;
        return info->kind;
    }

    media_result = pcfg_media_decode_header(header_bytes,
                                            store->config.format_magic,
                                            &info->header);
    if (media_result == PCFG_MEDIA_UNSUPPORTED)
    {
        info->kind = PCFG_SLOT_UNSUPPORTED;
        return info->kind;
    }
    if (media_result != PCFG_MEDIA_VALID)
    {
        info->kind = PCFG_SLOT_CORRUPT;
        return info->kind;
    }
    if ((info->header.payload_size == 0u) ||
        (info->header.payload_size > payload_limit))
    {
        info->kind = PCFG_SLOT_CORRUPT;
        return info->kind;
    }

    if (store->storage.read(
            store->storage_context,
            pcfg_slot_address(store, slot,
                              store->config.slot_size -
                              PCFG_MEDIA_COMMIT_SIZE),
            commit_bytes, sizeof(commit_bytes)) != PCFG_OK)
    {
        info->kind = PCFG_SLOT_STORAGE_ERROR;
        return info->kind;
    }
    media_result = pcfg_media_decode_commit(commit_bytes, &info->commit);
    if (media_result == PCFG_MEDIA_UNSUPPORTED)
    {
        info->kind = PCFG_SLOT_CORRUPT;
        return info->kind;
    }
    if (media_result != PCFG_MEDIA_VALID ||
        (info->commit.sequence != info->header.sequence) ||
        (info->commit.payload_crc32 != info->header.payload_crc32))
    {
        info->kind = PCFG_SLOT_CORRUPT;
        return info->kind;
    }

    info->kind = PCFG_SLOT_VALID_METADATA;
    return info->kind;
}

static pcfg_status_t pcfg_read_candidate(const pcfg_store_t *store, int slot,
                                         const pcfg_slot_info_t *info)
{
    uint32_t address = pcfg_slot_address(store, slot, PCFG_MEDIA_HEADER_SIZE);
    size_t remaining = (size_t)info->header.payload_size;
    uint32_t state = pcfg_crc32_init();
    size_t offset = 0u;

    if (remaining > store->scratch_size)
    {
        return PCFG_SCRATCH_TOO_SMALL;
    }
    if (store->storage.read(store->storage_context, address, store->scratch,
                            remaining) != PCFG_OK)
    {
        return PCFG_STORAGE_ERROR;
    }

    while (remaining != 0u)
    {
        size_t chunk = remaining;
        if (chunk > store->config.io_chunk_size)
        {
            chunk = store->config.io_chunk_size;
        }
        state = pcfg_crc32_update(state, store->scratch + offset, chunk);
        offset += chunk;
        remaining -= chunk;
    }
    if (pcfg_crc32_finish(state) != info->header.payload_crc32)
    {
        return PCFG_CORRUPT;
    }
    return PCFG_OK;
}

static bool pcfg_sequence_newer(uint32_t first, uint32_t second)
{
    return ((int32_t)(first - second)) > 0;
}

static pcfg_status_t pcfg_sequence_order(const pcfg_slot_info_t *first,
                                         const pcfg_slot_info_t *second,
                                         int *newest_slot)
{
    uint32_t difference = first->header.sequence - second->header.sequence;
    if (difference == UINT32_C(0x80000000))
    {
        return PCFG_SEQUENCE_AMBIGUOUS;
    }
    if ((difference == 0u) || !pcfg_sequence_newer(first->header.sequence,
                                                   second->header.sequence))
    {
        *newest_slot = 1;
    }
    else
    {
        *newest_slot = 0;
    }
    return PCFG_OK;
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
    pcfg_slot_info_t info[2];
    pcfg_status_t result;
    int valid_count = 0;
    int unknown_count = 0;
    int corrupt_count = 0;
    int blank_count = 0;
    int newest_slot = -1;
    int order[2];
    int order_count = 0;
    int slot;

    if ((store == NULL) || (payload == NULL) || (payload_size == NULL))
    {
        return PCFG_INVALID_ARGUMENT;
    }
    *payload_size = 0u;
    store->read_only = false;

    for (slot = 0; slot < 2; ++slot)
    {
        (void)pcfg_inspect_slot(store, slot, &info[slot]);
        if (info[slot].kind == PCFG_SLOT_STORAGE_ERROR)
        {
            return PCFG_STORAGE_ERROR;
        }
        if (info[slot].kind == PCFG_SLOT_VALID_METADATA)
        {
            valid_count++;
        }
        else if (info[slot].kind == PCFG_SLOT_UNSUPPORTED)
        {
            unknown_count++;
        }
        else if (info[slot].kind == PCFG_SLOT_CORRUPT)
        {
            corrupt_count++;
        }
        else
        {
            blank_count++;
        }
    }
    store->scan_complete = true;

    if (valid_count == 2)
    {
        result = pcfg_sequence_order(&info[0], &info[1], &newest_slot);
        if (result != PCFG_OK)
        {
            return result;
        }
        order[order_count++] = newest_slot;
        order[order_count++] = 1 - newest_slot;
    }
    else if (valid_count == 1)
    {
        order[order_count++] = (info[0].kind == PCFG_SLOT_VALID_METADATA) ? 0 : 1;
    }

    for (slot = 0; slot < order_count; ++slot)
    {
        int candidate = order[slot];
        size_t candidate_size = info[candidate].header.payload_size;
        result = pcfg_read_candidate(store, candidate, &info[candidate]);
        if (result == PCFG_STORAGE_ERROR)
        {
            return result;
        }
        if (result != PCFG_OK)
        {
            corrupt_count++;
            continue;
        }
        if (candidate_size > capacity)
        {
            *payload_size = candidate_size;
            return PCFG_BUFFER_TOO_SMALL;
        }
        if ((validate != NULL) &&
            (validate(validate_context, store->scratch, candidate_size) != PCFG_OK))
        {
            continue;
        }
        memcpy(payload, store->scratch, candidate_size);
        *payload_size = candidate_size;
        store->active_slot = candidate;
        store->active_sequence = info[candidate].header.sequence;
        store->read_only = (unknown_count != 0);
        return PCFG_OK;
    }

    store->active_slot = -1;
    store->active_sequence = 0u;
    if ((unknown_count != 0) && (valid_count == 0))
    {
        return PCFG_UNSUPPORTED_VERSION;
    }
    if ((valid_count != 0) && (corrupt_count == valid_count))
    {
        return (validate != NULL) ? PCFG_VALIDATION_FAILED : PCFG_CORRUPT;
    }
    if (corrupt_count != 0)
    {
        return PCFG_CORRUPT;
    }
    if ((blank_count == 2) && (unknown_count == 0))
    {
        return PCFG_NOT_FOUND;
    }
    return PCFG_CORRUPT;
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
