#include <stdint.h>
#include <string.h>

#include "flash_emulator.h"
#include "pcfg_media_internal.h"
#include "test_harness.h"

#define SAVE_FLASH_SIZE 8192u
#define SAVE_SLOT_SIZE 4096u

static uint8_t save_flash_bytes[SAVE_FLASH_SIZE];
static pcfg_flash_t save_flash;
static uint8_t save_scratch[SAVE_SLOT_SIZE];
static uint8_t save_output[SAVE_SLOT_SIZE];

static pcfg_config_t save_config(void)
{
    pcfg_config_t config;
    memset(&config, 0, sizeof(config));
    config.slot_address[0] = 0u;
    config.slot_address[1] = SAVE_SLOT_SIZE;
    config.slot_size = SAVE_SLOT_SIZE;
    config.erase_unit = 256u;
    config.program_unit = 4u;
    config.program_page_size = 256u;
    config.io_chunk_size = 64u;
    config.format_magic = PCFG_MEDIA_DEFAULT_MAGIC;
    return config;
}

static void save_setup(void)
{
    pcfg_flash_init(&save_flash, save_flash_bytes, sizeof(save_flash_bytes),
                    256u, 4u, 256u, 1);
}

static pcfg_status_t save_init(pcfg_store_t *store)
{
    pcfg_storage_ops_t ops = pcfg_flash_storage_ops();
    pcfg_config_t config = save_config();
    return pcfg_init(store, &config, &ops, &save_flash, save_scratch,
                     sizeof(save_scratch));
}

static pcfg_status_t reject_all(void *ctx, const uint8_t *payload,
                                size_t payload_size)
{
    (void)ctx;
    (void)payload;
    (void)payload_size;
    return PCFG_VALIDATION_FAILED;
}

static void test_first_save_and_round_trip(void)
{
    static const uint8_t expected[] = "first";
    pcfg_store_t store;
    size_t output_size = 0u;
    save_setup();
    TEST_ASSERT(save_init(&store) == PCFG_OK);
    TEST_ASSERT(pcfg_save(&store, expected, sizeof(expected) - 1u,
                          NULL, NULL) == PCFG_OK);
    TEST_ASSERT(store.active_slot == 0);
    TEST_ASSERT(store.active_sequence == 1u);
    TEST_ASSERT(pcfg_load(&store, save_output, sizeof(save_output),
                          &output_size, NULL, NULL) == PCFG_OK);
    TEST_ASSERT(output_size == sizeof(expected) - 1u);
    TEST_ASSERT_MEM_EQ(expected, save_output, output_size);
}

static void test_save_alternates_and_factory_reset_is_a_commit(void)
{
    static const uint8_t first[] = "first";
    static const uint8_t second[] = "second";
    static const uint8_t defaults[] = "defaults";
    pcfg_store_t store;
    size_t output_size = 0u;
    save_setup();
    TEST_ASSERT(save_init(&store) == PCFG_OK);
    TEST_ASSERT(pcfg_save(&store, first, sizeof(first) - 1u, NULL, NULL) ==
                PCFG_OK);
    TEST_ASSERT(pcfg_save(&store, second, sizeof(second) - 1u, NULL, NULL) ==
                PCFG_OK);
    TEST_ASSERT(store.active_slot == 1);
    TEST_ASSERT(store.active_sequence == 2u);
    TEST_ASSERT(pcfg_factory_reset(&store, defaults, sizeof(defaults) - 1u,
                                   NULL, NULL) == PCFG_OK);
    TEST_ASSERT(store.active_slot == 0);
    TEST_ASSERT(store.active_sequence == 3u);
    TEST_ASSERT(pcfg_load(&store, save_output, sizeof(save_output),
                          &output_size, NULL, NULL) == PCFG_OK);
    TEST_ASSERT_MEM_EQ(defaults, save_output, output_size);
}

static void test_validator_runs_before_storage_mutation(void)
{
    static const uint8_t rejected[] = "rejected";
    pcfg_store_t store;
    size_t operations_before;
    save_setup();
    TEST_ASSERT(save_init(&store) == PCFG_OK);
    operations_before = save_flash.operation_count;
    TEST_ASSERT(pcfg_save(&store, rejected, sizeof(rejected) - 1u,
                          reject_all, NULL) == PCFG_VALIDATION_FAILED);
    TEST_ASSERT(save_flash.operation_count == operations_before);
    TEST_ASSERT(store.active_slot == -1);
}

static void test_payload_limit_and_tail_padding(void)
{
    static uint8_t maximum[SAVE_SLOT_SIZE - PCFG_MEDIA_HEADER_SIZE -
                           PCFG_MEDIA_COMMIT_SIZE];
    static uint8_t too_large[SAVE_SLOT_SIZE - PCFG_MEDIA_HEADER_SIZE -
                             PCFG_MEDIA_COMMIT_SIZE + 1u];
    pcfg_store_t store;
    size_t index;
    memset(maximum, 0x3c, sizeof(maximum));
    memset(too_large, 0x5a, sizeof(too_large));
    save_setup();
    TEST_ASSERT(save_init(&store) == PCFG_OK);
    TEST_ASSERT(pcfg_save(&store, maximum, sizeof(maximum), NULL, NULL) ==
                PCFG_OK);
    TEST_ASSERT(pcfg_save(&store, too_large, sizeof(too_large), NULL, NULL) ==
                PCFG_PAYLOAD_TOO_LARGE);
    for (index = PCFG_MEDIA_HEADER_SIZE + sizeof(maximum);
         index < SAVE_SLOT_SIZE - PCFG_MEDIA_COMMIT_SIZE; ++index)
    {
        TEST_ASSERT(save_flash_bytes[index] == 0xffu);
    }
}

int main(void)
{
    RUN_TEST(test_first_save_and_round_trip);
    RUN_TEST(test_save_alternates_and_factory_reset_is_a_commit);
    RUN_TEST(test_validator_runs_before_storage_mutation);
    RUN_TEST(test_payload_limit_and_tail_padding);
    puts("test_save: PASS");
    return 0;
}
