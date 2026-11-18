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

typedef struct
{
    pcfg_slot_info_t info[2];
    int order[2];
    int order_count;
    int valid_count;
    int unknown_count;
    int corrupt_count;
    int blank_count;
} pcfg_scan_t;

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
        ((config->io_chunk_size % config->program_unit) != 0u) ||
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
    if ((media_result != PCFG_MEDIA_VALID) ||
        (info->commit.sequence != info->header.sequence) ||
        (info->commit.payload_crc32 != info->header.payload_crc32))
    {
        info->kind = PCFG_SLOT_CORRUPT;
        return info->kind;
    }

    info->kind = PCFG_SLOT_VALID_METADATA;
    return info->kind;
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
    if (difference == 0u)
    {
        *newest_slot = 0;
    }
    else if (pcfg_sequence_newer(first->header.sequence,
                                  second->header.sequence))
    {
        *newest_slot = 0;
    }
    else
    {
        *newest_slot = 1;
    }
    return PCFG_OK;
}

static pcfg_status_t pcfg_scan_metadata(pcfg_store_t *store, pcfg_scan_t *scan)
{
    int slot;
    pcfg_status_t result;
    int newest_slot;

    memset(scan, 0, sizeof(*scan));
    for (slot = 0; slot < 2; ++slot)
    {
        (void)pcfg_inspect_slot(store, slot, &scan->info[slot]);
        if (scan->info[slot].kind == PCFG_SLOT_STORAGE_ERROR)
        {
            return PCFG_STORAGE_ERROR;
        }
        if (scan->info[slot].kind == PCFG_SLOT_VALID_METADATA)
        {
            scan->valid_count++;
        }
        else if (scan->info[slot].kind == PCFG_SLOT_UNSUPPORTED)
        {
            scan->unknown_count++;
        }
        else if (scan->info[slot].kind == PCFG_SLOT_CORRUPT)
        {
            scan->corrupt_count++;
        }
        else
        {
            scan->blank_count++;
        }
    }

    store->scan_complete = true;
    store->read_only = (scan->unknown_count != 0);
    store->active_slot = -1;
    store->active_sequence = 0u;

    if (scan->valid_count == 2)
    {
        result = pcfg_sequence_order(&scan->info[0], &scan->info[1],
                                     &newest_slot);
        if (result != PCFG_OK)
        {
            return result;
        }
        scan->order[scan->order_count++] = newest_slot;
        scan->order[scan->order_count++] = 1 - newest_slot;
    }
    else if (scan->valid_count == 1)
    {
        scan->order[scan->order_count++] =
            (scan->info[0].kind == PCFG_SLOT_VALID_METADATA) ? 0 : 1;
    }

    if (scan->order_count != 0)
    {
        int active = scan->order[0];
        store->active_slot = active;
        store->active_sequence = scan->info[active].header.sequence;
        return PCFG_OK;
    }
    if (scan->unknown_count != 0)
    {
        return PCFG_UNSUPPORTED_VERSION;
    }
    if (scan->corrupt_count != 0)
    {
        return PCFG_CORRUPT;
    }
    return PCFG_NOT_FOUND;
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

static pcfg_status_t pcfg_program_aligned(pcfg_store_t *store, uint32_t address,
                                          const uint8_t *source, size_t size)
{
    size_t offset = 0u;

    if ((size == 0u) || ((address % store->config.program_unit) != 0u) ||
        ((size % store->config.program_unit) != 0u) ||
        (size > store->scratch_size))
    {
        return PCFG_INVALID_ARGUMENT;
    }

    while (offset < size)
    {
        size_t chunk = size - offset;
        size_t page_remaining;
        uint32_t current_address = address + (uint32_t)offset;

        if (chunk > store->config.io_chunk_size)
        {
            chunk = store->config.io_chunk_size;
        }
        if (store->config.program_page_size != 0u)
        {
            page_remaining = store->config.program_page_size -
                (current_address % store->config.program_page_size);
            if (chunk > page_remaining)
            {
                chunk = page_remaining;
            }
        }
        chunk -= chunk % store->config.program_unit;
        if (chunk == 0u)
        {
            return PCFG_UNSUPPORTED_GEOMETRY;
        }
        memcpy(store->scratch, source + offset, chunk);
        if (store->storage.program(store->storage_context, current_address,
                                   store->scratch, chunk) != PCFG_OK)
        {
            return PCFG_STORAGE_ERROR;
        }
        offset += chunk;
    }
    return PCFG_OK;
}

static pcfg_status_t pcfg_program_payload(pcfg_store_t *store, uint32_t address,
                                          const uint8_t *payload,
                                          size_t payload_size)
{
    size_t offset = 0u;

    if ((payload == store->scratch) || (payload_size == 0u))
    {
        return PCFG_INVALID_ARGUMENT;
    }
    while (offset < payload_size)
    {
        size_t remaining = payload_size - offset;
        size_t logical = remaining;
        size_t page_remaining;
        size_t programmed;
        uint32_t current_address = address + (uint32_t)offset;

        if (logical > store->config.io_chunk_size)
        {
            logical = store->config.io_chunk_size;
        }
        if (store->config.program_page_size != 0u)
        {
            page_remaining = store->config.program_page_size -
                (current_address % store->config.program_page_size);
            if (logical > page_remaining)
            {
                logical = page_remaining;
            }
        }
        logical -= logical % store->config.program_unit;
        if ((logical == 0u) && (remaining < store->config.program_unit))
        {
            logical = remaining;
        }
        if (logical == 0u)
        {
            return PCFG_UNSUPPORTED_GEOMETRY;
        }

        programmed = logical;
        if (logical == remaining)
        {
            programmed = (logical + store->config.program_unit - 1u) /
                         store->config.program_unit * store->config.program_unit;
        }
        if (programmed > store->scratch_size)
        {
            return PCFG_SCRATCH_TOO_SMALL;
        }
        memset(store->scratch, 0xff, programmed);
        memcpy(store->scratch, payload + offset, logical);
        if (store->storage.program(store->storage_context, current_address,
                                   store->scratch, programmed) != PCFG_OK)
        {
            return PCFG_STORAGE_ERROR;
        }
        offset += logical;
    }
    return PCFG_OK;
}

static pcfg_status_t pcfg_verify_payload(const pcfg_store_t *store, int slot,
                                         const pcfg_media_header_t *header)
{
    size_t remaining = header->payload_size;
    size_t offset = 0u;
    uint32_t state = pcfg_crc32_init();
    uint32_t address = pcfg_slot_address(store, slot, PCFG_MEDIA_HEADER_SIZE);

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
    return (pcfg_crc32_finish(state) == header->payload_crc32) ?
           PCFG_OK : PCFG_CORRUPT;
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
    store->reload_required = false;
    return PCFG_OK;
}

pcfg_status_t pcfg_load(pcfg_store_t *store,
                        uint8_t *payload,
                        size_t capacity,
                        size_t *payload_size,
                        pcfg_validate_fn validate,
                        void *validate_context)
{
    pcfg_scan_t scan;
    pcfg_status_t result;
    int order_index;
    int validation_failures = 0;
    int corrupt_candidates = 0;

    if ((store == NULL) || (payload == NULL) || (payload_size == NULL))
    {
        return PCFG_INVALID_ARGUMENT;
    }
    *payload_size = 0u;
    store->reload_required = false;
    result = pcfg_scan_metadata(store, &scan);
    if ((result != PCFG_OK) && (result != PCFG_CORRUPT) &&
        (result != PCFG_NOT_FOUND))
    {
        return result;
    }
    if (scan.order_count == 0)
    {
        return result;
    }

    for (order_index = 0; order_index < scan.order_count; ++order_index)
    {
        int candidate = scan.order[order_index];
        size_t candidate_size = scan.info[candidate].header.payload_size;
        result = pcfg_read_candidate(store, candidate, &scan.info[candidate]);
        if (result == PCFG_STORAGE_ERROR)
        {
            return result;
        }
        if (result == PCFG_SCRATCH_TOO_SMALL)
        {
            return result;
        }
        if (result != PCFG_OK)
        {
            corrupt_candidates++;
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
            validation_failures++;
            continue;
        }
        if (payload != store->scratch)
        {
            memcpy(payload, store->scratch, candidate_size);
        }
        *payload_size = candidate_size;
        store->active_slot = candidate;
        store->active_sequence = scan.info[candidate].header.sequence;
        return PCFG_OK;
    }

    store->active_slot = -1;
    store->active_sequence = 0u;
    if ((validation_failures != 0) && (corrupt_candidates == 0))
    {
        return PCFG_VALIDATION_FAILED;
    }
    if (corrupt_candidates != 0)
    {
        return PCFG_CORRUPT;
    }
    return result;
}

pcfg_status_t pcfg_save(pcfg_store_t *store,
                        const uint8_t *payload,
                        size_t payload_size,
                        pcfg_validate_fn validate,
                        void *validate_context)
{
    pcfg_scan_t scan;
    pcfg_status_t result;
    pcfg_media_header_t header;
    pcfg_media_commit_t commit;
    uint8_t header_bytes[PCFG_MEDIA_HEADER_SIZE];
    uint8_t commit_bytes[PCFG_MEDIA_COMMIT_SIZE];
    int target;
    uint32_t sequence;
    uint32_t target_address;
    size_t maximum_payload;

    if ((store == NULL) || (payload == NULL) || (payload == store->scratch) ||
        (payload_size == 0u))
    {
        return PCFG_INVALID_ARGUMENT;
    }
    maximum_payload = store->config.slot_size - PCFG_MEDIA_HEADER_SIZE -
                      PCFG_MEDIA_COMMIT_SIZE;
    if (payload_size > maximum_payload || payload_size > UINT32_MAX)
    {
        return PCFG_PAYLOAD_TOO_LARGE;
    }
    if (store->read_only)
    {
        return PCFG_READ_ONLY;
    }
    if (store->reload_required)
    {
        return PCFG_NOT_READY;
    }
    if ((validate != NULL) &&
        (validate(validate_context, payload, payload_size) != PCFG_OK))
    {
        return PCFG_VALIDATION_FAILED;
    }

    if (!store->scan_complete)
    {
        result = pcfg_scan_metadata(store, &scan);
        if ((result == PCFG_STORAGE_ERROR) ||
            (result == PCFG_UNSUPPORTED_VERSION) ||
            (result == PCFG_SEQUENCE_AMBIGUOUS))
        {
            return result;
        }
    }

    target = (store->active_slot == 0) ? 1 : 0;
    sequence = (store->active_slot < 0) ? 1u : store->active_sequence + 1u;
    target_address = store->config.slot_address[target];

    result = store->storage.erase(store->storage_context, target_address,
                                  store->config.slot_size);
    if (result != PCFG_OK)
    {
        store->reload_required = true;
        return PCFG_STORAGE_ERROR;
    }

    header.magic = store->config.format_magic;
    header.sequence = sequence;
    header.payload_size = (uint32_t)payload_size;
    header.payload_crc32 = pcfg_crc32(payload, payload_size);
    pcfg_media_encode_header(header_bytes, &header);
    result = pcfg_program_aligned(store, target_address, header_bytes,
                                  sizeof(header_bytes));
    if (result != PCFG_OK)
    {
        store->reload_required = true;
        return result;
    }

    result = pcfg_program_payload(store,
                                  target_address + PCFG_MEDIA_HEADER_SIZE,
                                  payload, payload_size);
    if (result != PCFG_OK)
    {
        store->reload_required = true;
        return result;
    }
    if ((store->storage.sync != NULL) &&
        (store->storage.sync(store->storage_context) != PCFG_OK))
    {
        store->reload_required = true;
        return PCFG_STORAGE_ERROR;
    }
    result = pcfg_verify_payload(store, target, &header);
    if (result != PCFG_OK)
    {
        store->reload_required = true;
        return result;
    }

    commit.sequence = sequence;
    commit.payload_crc32 = header.payload_crc32;
    pcfg_media_encode_commit(commit_bytes, &commit);
    result = pcfg_program_aligned(
        store,
        target_address + store->config.slot_size - PCFG_MEDIA_COMMIT_SIZE,
        commit_bytes, sizeof(commit_bytes));
    if (result != PCFG_OK)
    {
        store->reload_required = true;
        return result;
    }
    if ((store->storage.sync != NULL) &&
        (store->storage.sync(store->storage_context) != PCFG_OK))
    {
        store->reload_required = true;
        return PCFG_STORAGE_ERROR;
    }

    store->active_slot = target;
    store->active_sequence = sequence;
    store->scan_complete = true;
    store->read_only = false;
    store->reload_required = false;
    return PCFG_OK;
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

bool pcfg_reload_required(const pcfg_store_t *store)
{
    return (store != NULL) && store->reload_required;
}
