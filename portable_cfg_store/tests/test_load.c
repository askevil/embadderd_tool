#include <stdint.h>
#include <string.h>

#include "flash_emulator.h"
#include "pcfg_media_internal.h"
#include "test_harness.h"

#define LOAD_FLASH_SIZE 8192u
#define LOAD_SLOT_SIZE 4096u

static uint8_t load_flash_bytes[LOAD_FLASH_SIZE];
static pcfg_flash_t load_flash;
static uint8_t load_scratch[LOAD_SLOT_SIZE];
static uint8_t load_output[LOAD_SLOT_SIZE];
static const uint8_t untouched[4] = {0xa5, 0xa5, 0xa5, 0xa5};

static pcfg_config_t load_config(void)
{
    pcfg_config_t config;
    memset(&config, 0, sizeof(config));
    config.slot_address[0] = 0u;
    config.slot_address[1] = LOAD_SLOT_SIZE;
    config.slot_size = LOAD_SLOT_SIZE;
    config.erase_unit = 256u;
    config.program_unit = 4u;
    config.program_page_size = 256u;
    config.io_chunk_size = 64u;
    config.format_magic = PCFG_MEDIA_DEFAULT_MAGIC;
    return config;
}

static void load_setup(void)
{
    pcfg_flash_init(&load_flash, load_flash_bytes, sizeof(load_flash_bytes),
                    256u, 4u, 256u, 1);
    memset(load_output, 0xa5, sizeof(load_output));
}

static pcfg_status_t load_init(pcfg_store_t *store, uint8_t *scratch,
                               size_t scratch_size)
{
    pcfg_storage_ops_t ops = pcfg_flash_storage_ops();
    pcfg_config_t config = load_config();
    return pcfg_init(store, &config, &ops, &load_flash, scratch,
                     scratch_size);
}

static void encode_slot(size_t slot, uint32_t sequence, const uint8_t *payload,
                        size_t payload_size)
{
    pcfg_media_header_t header;
    pcfg_media_commit_t commit;
    uint8_t header_bytes[PCFG_MEDIA_HEADER_SIZE];
    uint8_t commit_bytes[PCFG_MEDIA_COMMIT_SIZE];
    uint32_t address = (uint32_t)(slot * LOAD_SLOT_SIZE);

    header.magic = PCFG_MEDIA_DEFAULT_MAGIC;
    header.sequence = sequence;
    header.payload_size = (uint32_t)payload_size;
    header.payload_crc32 = pcfg_crc32(payload, payload_size);
    pcfg_media_encode_header(header_bytes, &header);
    memcpy(load_flash_bytes + address, header_bytes, sizeof(header_bytes));
    memcpy(load_flash_bytes + address + PCFG_MEDIA_HEADER_SIZE,
           payload, payload_size);

    commit.sequence = sequence;
    commit.payload_crc32 = header.payload_crc32;
    pcfg_media_encode_commit(commit_bytes, &commit);
    memcpy(load_flash_bytes + address + LOAD_SLOT_SIZE - PCFG_MEDIA_COMMIT_SIZE,
           commit_bytes, sizeof(commit_bytes));
}

static pcfg_status_t reject_newest_only(void *ctx, const uint8_t *payload,
                                        size_t payload_size)
{
    static const uint8_t rejected[] = "newest";
    (void)ctx;
    if ((payload_size == sizeof(rejected) - 1u) &&
        (memcmp(payload, rejected, payload_size) == 0))
    {
        return PCFG_VALIDATION_FAILED;
    }
    return PCFG_OK;
}

static void test_blank_scan_returns_not_found_and_marks_scan_complete(void)
{
    pcfg_store_t store;
    size_t output_size = 99u;
    load_setup();
    TEST_ASSERT(load_init(&store, load_scratch, sizeof(load_scratch)) == PCFG_OK);
    TEST_ASSERT(pcfg_load(&store, load_output, sizeof(load_output),
                          &output_size, NULL, NULL) == PCFG_NOT_FOUND);
    TEST_ASSERT(output_size == 0u);
    TEST_ASSERT(store.scan_complete == true);
    TEST_ASSERT(store.active_slot == -1);
    TEST_ASSERT_MEM_EQ(untouched, load_output, 4u);
}

static void test_newest_valid_slot_loads_and_reports_state(void)
{
    static const uint8_t old_payload[] = "old";
    static const uint8_t new_payload[] = "newest";
    pcfg_store_t store;
    size_t output_size = 0u;
    load_setup();
    encode_slot(0u, 4u, old_payload, sizeof(old_payload) - 1u);
    encode_slot(1u, 5u, new_payload, sizeof(new_payload) - 1u);
    TEST_ASSERT(load_init(&store, load_scratch, sizeof(load_scratch)) == PCFG_OK);
    TEST_ASSERT(pcfg_load(&store, load_output, sizeof(load_output),
                          &output_size, NULL, NULL) == PCFG_OK);
    TEST_ASSERT(output_size == sizeof(new_payload) - 1u);
    TEST_ASSERT_MEM_EQ(new_payload, load_output, output_size);
    TEST_ASSERT(store.active_slot == 1);
    TEST_ASSERT(store.active_sequence == 5u);
}

static void test_corrupt_newest_falls_back_to_older(void)
{
    static const uint8_t old_payload[] = "old";
    static const uint8_t new_payload[] = "newest";
    pcfg_store_t store;
    size_t output_size = 0u;
    load_setup();
    encode_slot(0u, 4u, old_payload, sizeof(old_payload) - 1u);
    encode_slot(1u, 5u, new_payload, sizeof(new_payload) - 1u);
    load_flash_bytes[LOAD_SLOT_SIZE + PCFG_MEDIA_HEADER_SIZE] ^= 1u;
    TEST_ASSERT(load_init(&store, load_scratch, sizeof(load_scratch)) == PCFG_OK);
    TEST_ASSERT(pcfg_load(&store, load_output, sizeof(load_output),
                          &output_size, NULL, NULL) == PCFG_OK);
    TEST_ASSERT(output_size == sizeof(old_payload) - 1u);
    TEST_ASSERT_MEM_EQ(old_payload, load_output, output_size);
    TEST_ASSERT(store.active_slot == 0);
}

static void test_semantic_invalid_newest_falls_back_to_older(void)
{
    static const uint8_t old_payload[] = "old";
    static const uint8_t new_payload[] = "newest";
    pcfg_store_t store;
    size_t output_size = 0u;
    load_setup();
    encode_slot(0u, 4u, old_payload, sizeof(old_payload) - 1u);
    encode_slot(1u, 5u, new_payload, sizeof(new_payload) - 1u);
    TEST_ASSERT(load_init(&store, load_scratch, sizeof(load_scratch)) == PCFG_OK);
    TEST_ASSERT(pcfg_load(&store, load_output, sizeof(load_output),
                          &output_size, reject_newest_only, NULL) ==
                PCFG_OK);
    TEST_ASSERT(output_size == sizeof(old_payload) - 1u);
    TEST_ASSERT_MEM_EQ(old_payload, load_output, output_size);
}

static void test_small_output_reports_required_size_and_preserves_output(void)
{
    static const uint8_t payload[] = "large-enough";
    pcfg_store_t store;
    size_t output_size = 0u;
    load_setup();
    encode_slot(0u, 1u, payload, sizeof(payload) - 1u);
    TEST_ASSERT(load_init(&store, load_scratch, sizeof(load_scratch)) == PCFG_OK);
    TEST_ASSERT(pcfg_load(&store, load_output, 3u, &output_size, NULL, NULL) ==
                PCFG_BUFFER_TOO_SMALL);
    TEST_ASSERT(output_size == sizeof(payload) - 1u);
    TEST_ASSERT_MEM_EQ(untouched, load_output, 4u);
}

static void test_wrap_safe_sequence_order_and_equal_tie_break(void)
{
    static const uint8_t zero_payload[] = "zero";
    static const uint8_t max_payload[] = "max";
    pcfg_store_t store;
    size_t output_size = 0u;
    load_setup();
    encode_slot(0u, UINT32_MAX, max_payload, sizeof(max_payload) - 1u);
    encode_slot(1u, 0u, zero_payload, sizeof(zero_payload) - 1u);
    TEST_ASSERT(load_init(&store, load_scratch, sizeof(load_scratch)) == PCFG_OK);
    TEST_ASSERT(pcfg_load(&store, load_output, sizeof(load_output),
                          &output_size, NULL, NULL) == PCFG_OK);
    TEST_ASSERT(store.active_slot == 1);
    TEST_ASSERT(store.active_sequence == 0u);
}

static void test_unknown_newer_slot_makes_supported_load_read_only(void)
{
    static const uint8_t old_payload[] = "supported";
    pcfg_store_t store;
    size_t output_size = 0u;
    uint32_t address = LOAD_SLOT_SIZE;
    load_setup();
    encode_slot(0u, 7u, old_payload, sizeof(old_payload) - 1u);
    load_flash_bytes[address + 4u] = 2u;
    load_flash_bytes[address + 5u] = 0u;
    TEST_ASSERT(load_init(&store, load_scratch, sizeof(load_scratch)) == PCFG_OK);
    TEST_ASSERT(pcfg_load(&store, load_output, sizeof(load_output),
                          &output_size, NULL, NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_is_read_only(&store) == true);
    TEST_ASSERT(output_size == sizeof(old_payload) - 1u);
}

static void test_unknown_only_returns_unsupported(void)
{
    pcfg_store_t store;
    size_t output_size = 0u;
    load_setup();
    load_flash_bytes[LOAD_SLOT_SIZE + 0u] = 0x31u;
    load_flash_bytes[LOAD_SLOT_SIZE + 1u] = 0x46u;
    load_flash_bytes[LOAD_SLOT_SIZE + 2u] = 0x43u;
    load_flash_bytes[LOAD_SLOT_SIZE + 3u] = 0x50u;
    load_flash_bytes[LOAD_SLOT_SIZE + 4u] = 2u;
    TEST_ASSERT(load_init(&store, load_scratch, sizeof(load_scratch)) == PCFG_OK);
    TEST_ASSERT(pcfg_load(&store, load_output, sizeof(load_output),
                          &output_size, NULL, NULL) == PCFG_UNSUPPORTED_VERSION);
}

int main(void)
{
    RUN_TEST(test_blank_scan_returns_not_found_and_marks_scan_complete);
    RUN_TEST(test_newest_valid_slot_loads_and_reports_state);
    RUN_TEST(test_corrupt_newest_falls_back_to_older);
    RUN_TEST(test_semantic_invalid_newest_falls_back_to_older);
    RUN_TEST(test_small_output_reports_required_size_and_preserves_output);
    RUN_TEST(test_wrap_safe_sequence_order_and_equal_tie_break);
    RUN_TEST(test_unknown_newer_slot_makes_supported_load_read_only);
    RUN_TEST(test_unknown_only_returns_unsupported);
    puts("test_load: PASS");
    return 0;
}
