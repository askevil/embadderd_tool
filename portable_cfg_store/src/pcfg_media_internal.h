#ifndef PCFG_MEDIA_INTERNAL_H
#define PCFG_MEDIA_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pcfg_crc32.h"

#define PCFG_MEDIA_HEADER_SIZE 32u
#define PCFG_MEDIA_COMMIT_SIZE 16u
#define PCFG_MEDIA_FORMAT_VERSION 1u
#define PCFG_MEDIA_DEFAULT_MAGIC UINT32_C(0x50434631)
#define PCFG_MEDIA_COMMIT_MAGIC UINT32_C(0x434d5431)

typedef enum
{
    PCFG_MEDIA_VALID = 0,
    PCFG_MEDIA_CORRUPT,
    PCFG_MEDIA_UNSUPPORTED
} pcfg_media_result_t;

typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    uint32_t payload_size;
    uint32_t payload_crc32;
} pcfg_media_header_t;

typedef struct
{
    uint32_t sequence;
    uint32_t payload_crc32;
} pcfg_media_commit_t;

static uint16_t pcfg_get_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8u));
}

static uint32_t pcfg_get_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}

static void pcfg_put_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
}

static void pcfg_put_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}

static uint32_t pcfg_media_header_crc(const uint8_t bytes[PCFG_MEDIA_HEADER_SIZE])
{
    uint32_t state = pcfg_crc32_init();
    state = pcfg_crc32_update(state, bytes, 24u);
    state = pcfg_crc32_update(state, bytes + 28u, 4u);
    return pcfg_crc32_finish(state);
}

static void pcfg_media_encode_header(uint8_t bytes[PCFG_MEDIA_HEADER_SIZE],
                                     const pcfg_media_header_t *header)
{
    memset(bytes, 0, PCFG_MEDIA_HEADER_SIZE);
    pcfg_put_le32(bytes + 0u, header->magic);
    pcfg_put_le16(bytes + 4u, PCFG_MEDIA_FORMAT_VERSION);
    pcfg_put_le16(bytes + 6u, PCFG_MEDIA_HEADER_SIZE);
    pcfg_put_le32(bytes + 8u, header->sequence);
    pcfg_put_le32(bytes + 12u, header->payload_size);
    pcfg_put_le32(bytes + 16u, header->payload_crc32);
    pcfg_put_le32(bytes + 24u, pcfg_media_header_crc(bytes));
}

static pcfg_media_result_t pcfg_media_decode_header(
    const uint8_t bytes[PCFG_MEDIA_HEADER_SIZE],
    uint32_t expected_magic,
    pcfg_media_header_t *header)
{
    if ((pcfg_get_le32(bytes + 0u) != expected_magic) ||
        (pcfg_get_le16(bytes + 4u) != PCFG_MEDIA_FORMAT_VERSION) ||
        (pcfg_get_le16(bytes + 6u) != PCFG_MEDIA_HEADER_SIZE) ||
        (pcfg_get_le32(bytes + 20u) != 0u) ||
        (pcfg_get_le32(bytes + 28u) != 0u))
    {
        return PCFG_MEDIA_UNSUPPORTED;
    }

    if (pcfg_get_le32(bytes + 24u) != pcfg_media_header_crc(bytes))
    {
        return PCFG_MEDIA_CORRUPT;
    }

    header->magic = expected_magic;
    header->sequence = pcfg_get_le32(bytes + 8u);
    header->payload_size = pcfg_get_le32(bytes + 12u);
    header->payload_crc32 = pcfg_get_le32(bytes + 16u);
    return PCFG_MEDIA_VALID;
}

static void pcfg_media_encode_commit(uint8_t bytes[PCFG_MEDIA_COMMIT_SIZE],
                                     const pcfg_media_commit_t *commit)
{
    memset(bytes, 0, PCFG_MEDIA_COMMIT_SIZE);
    pcfg_put_le32(bytes + 0u, PCFG_MEDIA_COMMIT_MAGIC);
    pcfg_put_le32(bytes + 4u, commit->sequence);
    pcfg_put_le32(bytes + 8u, commit->payload_crc32);
    pcfg_put_le32(bytes + 12u, pcfg_crc32(bytes, 12u));
}

static pcfg_media_result_t pcfg_media_decode_commit(
    const uint8_t bytes[PCFG_MEDIA_COMMIT_SIZE],
    pcfg_media_commit_t *commit)
{
    if (pcfg_get_le32(bytes + 0u) != PCFG_MEDIA_COMMIT_MAGIC)
    {
        return PCFG_MEDIA_UNSUPPORTED;
    }
    if (pcfg_get_le32(bytes + 12u) != pcfg_crc32(bytes, 12u))
    {
        return PCFG_MEDIA_CORRUPT;
    }

    commit->sequence = pcfg_get_le32(bytes + 4u);
    commit->payload_crc32 = pcfg_get_le32(bytes + 8u);
    return PCFG_MEDIA_VALID;
}

#endif
