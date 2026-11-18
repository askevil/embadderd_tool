#include <stdint.h>
#include <string.h>

#include "flash_emulator.h"
#include "test_harness.h"

#define FLASH_SIZE 8192u
#define SLOT_SIZE 4096u

static uint8_t flash_bytes[FLASH_SIZE];
static pcfg_flash_t flash;
static uint32_t scratch_words[32];

static pcfg_config_t valid_config(void)
{
    pcfg_config_t config;
    memset(&config, 0, sizeof(config));
    config.slot_address[0] = 0u;
    config.slot_address[1] = SLOT_SIZE;
    config.slot_size = SLOT_SIZE;
    config.erase_unit = 256u;
    config.program_unit = 4u;
    config.program_page_size = 256u;
    config.io_chunk_size = 64u;
    config.format_magic = UINT32_C(0x50434631);
    return config;
}

static void setup_flash(void)
{
    pcfg_flash_init(&flash, flash_bytes, sizeof(flash_bytes), 256u, 4u,
                    256u, 1);
}

static pcfg_status_t init_with(pcfg_store_t *store, pcfg_config_t config,
                               void *scratch, size_t scratch_size)
{
    pcfg_storage_ops_t ops = pcfg_flash_storage_ops();
    return pcfg_init(store, &config, &ops, &flash, scratch, scratch_size);
}

static void test_valid_geometry_initializes_without_io(void)
{
    pcfg_store_t store;
    pcfg_config_t config = valid_config();
    setup_flash();
    TEST_ASSERT(init_with(&store, config, scratch_words,
                          sizeof(scratch_words)) == PCFG_OK);
    TEST_ASSERT(store.active_slot == -1);
    TEST_ASSERT(store.active_sequence == 0u);
    TEST_ASSERT(store.scan_complete == false);
    TEST_ASSERT(store.read_only == false);
    TEST_ASSERT(flash.operation_count == 0u);
}

static void test_invalid_geometry_is_rejected_without_io(void)
{
    pcfg_store_t store;
    pcfg_config_t config;

    setup_flash();
    config = valid_config();
    config.slot_address[1] = 2048u;
    TEST_ASSERT(init_with(&store, config, scratch_words,
                          sizeof(scratch_words)) == PCFG_UNSUPPORTED_GEOMETRY);

    config = valid_config();
    config.slot_address[0] = 1u;
    TEST_ASSERT(init_with(&store, config, scratch_words,
                          sizeof(scratch_words)) == PCFG_UNSUPPORTED_GEOMETRY);

    config = valid_config();
    config.program_unit = 3u;
    TEST_ASSERT(init_with(&store, config, scratch_words,
                          sizeof(scratch_words)) == PCFG_UNSUPPORTED_GEOMETRY);

    config = valid_config();
    config.program_page_size = 6u;
    TEST_ASSERT(init_with(&store, config, scratch_words,
                          sizeof(scratch_words)) == PCFG_UNSUPPORTED_GEOMETRY);

    config = valid_config();
    config.slot_size = 48u;
    TEST_ASSERT(init_with(&store, config, scratch_words,
                          sizeof(scratch_words)) == PCFG_UNSUPPORTED_GEOMETRY);

    config = valid_config();
    config.slot_address[1] = UINT32_MAX - 2047u;
    TEST_ASSERT(init_with(&store, config, scratch_words,
                          sizeof(scratch_words)) == PCFG_UNSUPPORTED_GEOMETRY);

    config = valid_config();
    config.io_chunk_size = 0u;
    TEST_ASSERT(init_with(&store, config, scratch_words,
                          sizeof(scratch_words)) == PCFG_UNSUPPORTED_GEOMETRY);

    config = valid_config();
    config.io_chunk_size = 256u;
    TEST_ASSERT(init_with(&store, config, scratch_words,
                          sizeof(scratch_words)) == PCFG_UNSUPPORTED_GEOMETRY);

    config = valid_config();
    TEST_ASSERT(init_with(&store, config, ((uint8_t *)scratch_words) + 1u,
                          sizeof(scratch_words) - 1u) ==
                PCFG_UNSUPPORTED_GEOMETRY);

    TEST_ASSERT(flash.operation_count == 0u);
}

static void test_missing_storage_callbacks_are_rejected(void)
{
    pcfg_store_t store;
    pcfg_config_t config = valid_config();
    pcfg_storage_ops_t ops = pcfg_flash_storage_ops();
    setup_flash();

    ops.read = NULL;
    TEST_ASSERT(pcfg_init(&store, &config, &ops, &flash, scratch_words,
                          sizeof(scratch_words)) == PCFG_INVALID_ARGUMENT);
    ops = pcfg_flash_storage_ops();
    TEST_ASSERT(pcfg_init(NULL, &config, &ops, &flash,
                          scratch_words, sizeof(scratch_words)) ==
                PCFG_INVALID_ARGUMENT);
}

static void test_flash_emulator_enforces_nor_programming(void)
{
    pcfg_storage_ops_t ops;
    const uint8_t low[4] = {0x0fu, 0xf0u, 0x55u, 0xaau};
    const uint8_t high[4] = {0xffu, 0xffu, 0xffu, 0xffu};
    uint8_t before[4];

    setup_flash();
    ops = pcfg_flash_storage_ops();
    TEST_ASSERT(ops.program(&flash, 0u, low, sizeof(low)) == PCFG_OK);
    memcpy(before, flash_bytes, sizeof(before));
    TEST_ASSERT(ops.program(&flash, 0u, high, sizeof(high)) ==
                PCFG_STORAGE_ERROR);
    TEST_ASSERT_MEM_EQ(before, flash_bytes, sizeof(before));
}

static void test_flash_emulator_rejects_alignment_page_and_range_errors(void)
{
    pcfg_storage_ops_t ops;
    uint8_t data[8] = {0};
    uint8_t output[8] = {0};

    setup_flash();
    ops = pcfg_flash_storage_ops();
    TEST_ASSERT(ops.erase(&flash, 1u, 256u) == PCFG_STORAGE_ERROR);
    TEST_ASSERT(ops.program(&flash, 252u, data, sizeof(data)) ==
                PCFG_STORAGE_ERROR);
    TEST_ASSERT(ops.program(&flash, 2u, data, 4u) == PCFG_STORAGE_ERROR);
    TEST_ASSERT(ops.read(&flash, FLASH_SIZE - 4u, output, sizeof(output)) ==
                PCFG_STORAGE_ERROR);
    TEST_ASSERT(flash.mutation_count == 0u);
}

int main(void)
{
    RUN_TEST(test_valid_geometry_initializes_without_io);
    RUN_TEST(test_invalid_geometry_is_rejected_without_io);
    RUN_TEST(test_missing_storage_callbacks_are_rejected);
    RUN_TEST(test_flash_emulator_enforces_nor_programming);
    RUN_TEST(test_flash_emulator_rejects_alignment_page_and_range_errors);
    puts("test_store: PASS");
    return 0;
}
