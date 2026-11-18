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

static size_t fault_cases;

static void sweep_transaction(int reset, int prior_generations, int cold_start)
{
    static const uint8_t old_payload[] = "old-payload";
    static const uint8_t new_payload[] = "new-payload-with-more-bytes";
    size_t sizes[256];
    size_t operations;
    size_t operation;
    pcfg_store_t baseline;
    int generation;
    fault_setup();
    TEST_ASSERT(fault_init(&baseline) == PCFG_OK);
    for (generation = 0; generation < prior_generations; ++generation)
        TEST_ASSERT(pcfg_save(&baseline,old_payload,sizeof(old_payload)-1u,NULL,NULL) == PCFG_OK);
    if (cold_start) TEST_ASSERT(fault_init(&baseline)==PCFG_OK);
    pcfg_flash_reset_faults(&fault_flash);
    TEST_ASSERT(pcfg_save(&baseline,new_payload,sizeof(new_payload)-1u,NULL,NULL) == PCFG_OK);
    operations = fault_flash.operation_count;
    TEST_ASSERT(operations <= 256u);
    memcpy(sizes,fault_flash.operation_size,sizeof(sizes));
    for (operation = 1u; operation <= operations; ++operation)
    {
        size_t prefix;
        for (prefix = 0u; prefix <= sizes[operation-1u]; ++prefix)
        {
            int persist;
            for (persist = 0; persist <= 1; ++persist)
            {
                pcfg_store_t writer;
                pcfg_store_t reader;
                size_t loaded_size = 0u;
                pcfg_status_t result;
                fault_setup();
                TEST_ASSERT(fault_init(&writer) == PCFG_OK);
                for (generation = 0; generation < prior_generations; ++generation)
                    TEST_ASSERT(pcfg_save(&writer,old_payload,sizeof(old_payload)-1u,NULL,NULL) == PCFG_OK);
                if (cold_start) TEST_ASSERT(fault_init(&writer)==PCFG_OK);
                pcfg_flash_reset_faults(&fault_flash);
                pcfg_flash_fail_operation(&fault_flash,operation,prefix,persist);
                result = reset ? pcfg_factory_reset(&writer,new_payload,sizeof(new_payload)-1u,NULL,NULL) :
                                 pcfg_save(&writer,new_payload,sizeof(new_payload)-1u,NULL,NULL);
                TEST_ASSERT(result != PCFG_OK);
                TEST_ASSERT(fault_flash.fault_hits == 1u);
                TEST_ASSERT(fault_init(&reader) == PCFG_OK);
                result = pcfg_load(&reader,fault_output,sizeof(fault_output),&loaded_size,NULL,NULL);
                TEST_ASSERT(result == PCFG_OK);
                TEST_ASSERT(payload_equals(old_payload,sizeof(old_payload)-1u,fault_output,loaded_size) ||
                            payload_equals(new_payload,sizeof(new_payload)-1u,fault_output,loaded_size));
                result=pcfg_save(&reader,new_payload,sizeof(new_payload)-1u,NULL,NULL);
                if(result!=PCFG_OK) fprintf(stderr,"retry reset=%d prior=%d cold=%d op=%lu prefix=%lu persist=%d status=%d readonly=%d\n",reset,prior_generations,cold_start,(unsigned long)operation,(unsigned long)prefix,persist,(int)result,(int)pcfg_is_read_only(&reader));
                TEST_ASSERT(result==PCFG_OK);
                TEST_ASSERT(fault_init(&reader)==PCFG_OK);
                TEST_ASSERT(pcfg_load(&reader,fault_output,sizeof(fault_output),&loaded_size,NULL,NULL)==PCFG_OK);
                TEST_ASSERT(payload_equals(new_payload,sizeof(new_payload)-1u,fault_output,loaded_size));
                fault_cases++;
            }
        }
    }
}
static void test_every_injected_save_failure_recovers_old_or_new(void)
{
    sweep_transaction(0,1,0);
    sweep_transaction(0,2,1);
    sweep_transaction(0,2,0); /* partially erased target already contains a valid older generation */
}
static void test_factory_reset_failure_recovers_old_or_default(void)
{
    sweep_transaction(1,2,0);
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
    printf("fault points exercised: %lu\n", (unsigned long)fault_cases);
    puts("test_fault_injection: PASS");
    return 0;
}
