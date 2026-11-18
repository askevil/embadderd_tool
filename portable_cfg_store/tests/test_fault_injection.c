#include <stdint.h>
#include <string.h>

#include "flash_emulator.h"
#include "pcfg_media_internal.h"
#include "test_harness.h"

#define FAULT_FLASH_SIZE 8192u
#define FAULT_SLOT_SIZE 4096u

static uint8_t fault_flash_bytes[FAULT_FLASH_SIZE];
static pcfg_flash_t fault_flash;
static uint8_t fault_scratch[FAULT_SLOT_SIZE];
static uint8_t fault_output[FAULT_SLOT_SIZE];

static pcfg_config_t fault_config(void)
{
    pcfg_config_t config;
    memset(&config, 0, sizeof(config));
    config.slot_address[0] = 0u;
    config.slot_address[1] = FAULT_SLOT_SIZE;
    config.slot_size = FAULT_SLOT_SIZE;
    config.erase_unit = 256u;
    config.program_unit = 4u;
    config.program_page_size = 256u;
    config.io_chunk_size = 64u;
    config.format_magic = PCFG_MEDIA_DEFAULT_MAGIC;
    return config;
}

static void fault_setup(void)
{
    pcfg_flash_init(&fault_flash, fault_flash_bytes, sizeof(fault_flash_bytes),
                    256u, 4u, 256u, 1);
}

static pcfg_status_t fault_init(pcfg_store_t *store)
{
    pcfg_storage_ops_t ops = pcfg_flash_storage_ops();
    pcfg_config_t config = fault_config();
    return pcfg_init(store, &config, &ops, &fault_flash, fault_scratch,
                     sizeof(fault_scratch));
}

static void commit_payload(const uint8_t *payload, size_t payload_size)
{
    pcfg_store_t store;
    TEST_ASSERT(fault_init(&store) == PCFG_OK);
    TEST_ASSERT(pcfg_save(&store, payload, payload_size, NULL, NULL) == PCFG_OK);
}

static int payload_equals(const uint8_t *expected, size_t expected_size,
                          const uint8_t *actual, size_t actual_size)
{
    return (expected_size == actual_size) &&
           (memcmp(expected, actual, expected_size) == 0);
}

static void test_every_injected_save_failure_recovers_old_or_new(void)
{
    static const uint8_t old_payload[] = "old-payload";
    static const uint8_t new_payload[] = "new-payload-with-more-bytes";
    size_t baseline_operations;
    size_t operation;
    size_t after_byte;

    fault_setup();
    commit_payload(old_payload, sizeof(old_payload) - 1u);
    {
        pcfg_store_t baseline;
        pcfg_flash_reset_faults(&fault_flash);
        TEST_ASSERT(fault_init(&baseline) == PCFG_OK);
        TEST_ASSERT(pcfg_save(&baseline, new_payload, sizeof(new_payload) - 1u,
                              NULL, NULL) == PCFG_OK);
        baseline_operations = fault_flash.operation_count;
    }

    for (operation = 1u; operation <= baseline_operations + 32u; ++operation)
    {
        for (after_byte = 0u; after_byte <= 8u; after_byte += 4u)
        {
            pcfg_store_t writer;
            pcfg_store_t reader;
            size_t loaded_size = 0u;
            pcfg_status_t save_result;
            int is_old;
            int is_new;

            fault_setup();
            commit_payload(old_payload, sizeof(old_payload) - 1u);
            pcfg_flash_reset_faults(&fault_flash);
            TEST_ASSERT(fault_init(&writer) == PCFG_OK);
            pcfg_flash_fail_operation(&fault_flash, operation, after_byte, 1);
            save_result = pcfg_save(&writer, new_payload,
                                    sizeof(new_payload) - 1u, NULL, NULL);
            if (save_result == PCFG_OK)
            {
                continue;
            }
            TEST_ASSERT(fault_init(&reader) == PCFG_OK);
            memset(fault_output, 0xa5, sizeof(fault_output));
            save_result = pcfg_load(&reader, fault_output, sizeof(fault_output),
                                    &loaded_size, NULL, NULL);
            is_old = payload_equals(old_payload, sizeof(old_payload) - 1u,
                                    fault_output, loaded_size);
            is_new = payload_equals(new_payload, sizeof(new_payload) - 1u,
                                    fault_output, loaded_size);
            TEST_ASSERT(save_result == PCFG_OK);
            TEST_ASSERT((is_old != 0) || (is_new != 0));
        }
    }
}

static void test_factory_reset_failure_recovers_old_or_default(void)
{
    static const uint8_t old_payload[] = "old";
    static const uint8_t defaults[] = "factory-default";
    pcfg_store_t writer;
    pcfg_store_t reader;
    size_t operation;
    size_t loaded_size;

    fault_setup();
    commit_payload(old_payload, sizeof(old_payload) - 1u);
    for (operation = 1u; operation <= 40u; ++operation)
    {
        int is_old;
        int is_default;
        pcfg_status_t result;
        fault_setup();
        commit_payload(old_payload, sizeof(old_payload) - 1u);
        pcfg_flash_reset_faults(&fault_flash);
        TEST_ASSERT(fault_init(&writer) == PCFG_OK);
        pcfg_flash_fail_operation(&fault_flash, operation, 0u, 1);
        result = pcfg_factory_reset(&writer, defaults, sizeof(defaults) - 1u,
                                    NULL, NULL);
        if (result == PCFG_OK)
        {
            continue;
        }
        TEST_ASSERT(fault_init(&reader) == PCFG_OK);
        loaded_size = 0u;
        result = pcfg_load(&reader, fault_output, sizeof(fault_output),
                           &loaded_size, NULL, NULL);
        is_old = payload_equals(old_payload, sizeof(old_payload) - 1u,
                                fault_output, loaded_size);
        is_default = payload_equals(defaults, sizeof(defaults) - 1u,
                                    fault_output, loaded_size);
        TEST_ASSERT(result == PCFG_OK);
        TEST_ASSERT((is_old != 0) || (is_default != 0));
    }
}

static void test_failed_save_requires_reload_before_retry(void)
{
    static const uint8_t old_payload[] = "old";
    static const uint8_t new_payload[] = "new";
    pcfg_store_t store;
    size_t output_size = 0u;

    fault_setup();
    commit_payload(old_payload, sizeof(old_payload) - 1u);
    pcfg_flash_reset_faults(&fault_flash);
    TEST_ASSERT(fault_init(&store) == PCFG_OK);
    pcfg_flash_fail_operation(&fault_flash, 4u, 0u, 1);
    TEST_ASSERT(pcfg_save(&store, new_payload, sizeof(new_payload) - 1u,
                          NULL, NULL) != PCFG_OK);
    TEST_ASSERT(pcfg_reload_required(&store) == true);
    TEST_ASSERT(pcfg_save(&store, new_payload, sizeof(new_payload) - 1u,
                          NULL, NULL) == PCFG_NOT_READY);
    TEST_ASSERT(pcfg_load(&store, fault_output, sizeof(fault_output),
                          &output_size, NULL, NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_reload_required(&store) == false);
}

int main(void)
{
    RUN_TEST(test_every_injected_save_failure_recovers_old_or_new);
    RUN_TEST(test_factory_reset_failure_recovers_old_or_default);
    RUN_TEST(test_failed_save_requires_reload_before_retry);
    puts("test_fault_injection: PASS");
    return 0;
}
