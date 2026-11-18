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

static void test_small_scratch_save_streams_and_verifies(void)
{
    pcfg_store_t store;
    pcfg_config_t cfg=save_config();
    pcfg_storage_ops_t ops=pcfg_flash_storage_ops();
    static uint32_t small_scratch[4];
    uint8_t payload[101];
    size_t size=0;
    memset(payload,0x3c,sizeof(payload));
    cfg.io_chunk_size=16;
    cfg.program_unit=16;
    save_setup(); save_flash.program_unit=16;
    TEST_ASSERT(pcfg_init(&store,&cfg,&ops,&save_flash,small_scratch,sizeof(small_scratch))==PCFG_OK);
    TEST_ASSERT(pcfg_save(&store,payload,sizeof(payload),NULL,NULL)==PCFG_OK);
    TEST_ASSERT(save_init(&store)==PCFG_OK);
    TEST_ASSERT(pcfg_load(&store,save_output,sizeof(save_output),&size,NULL,NULL)==PCFG_OK);
    TEST_ASSERT(size==sizeof(payload));
    TEST_ASSERT_MEM_EQ(payload,save_output,size);
}
static pcfg_read_fn original_read;
static int fail_metadata_read;
static pcfg_status_t read_with_metadata_error(void *ctx,uint32_t address,void *buffer,size_t size)
{
    if ((fail_metadata_read==1 && address==0 && size==32) ||
        (fail_metadata_read==2 && address==SAVE_SLOT_SIZE-16 && size==16))
        return PCFG_STORAGE_ERROR;
    return original_read(ctx,address,buffer,size);
}
static void test_save_verifies_header_and_final_commit(void)
{
    static const uint8_t payload[]="verified";
    pcfg_store_t store;
    int phase;
    for (phase=1;phase<=2;++phase)
    {
        size_t size=0;
        save_setup(); TEST_ASSERT(save_init(&store)==PCFG_OK);
        TEST_ASSERT(pcfg_load(&store,save_output,sizeof(save_output),&size,NULL,NULL)==PCFG_NOT_FOUND);
        original_read=store.storage.read;
        store.storage.read=read_with_metadata_error;
        fail_metadata_read=phase;
        TEST_ASSERT(pcfg_save(&store,payload,sizeof(payload)-1u,NULL,NULL)==PCFG_STORAGE_ERROR);
        TEST_ASSERT(pcfg_reload_required(&store));
    }
    fail_metadata_read=0;
}

static pcfg_sync_fn original_sync;
static int corrupt_phase;
static int sync_calls;
static pcfg_status_t sync_with_corruption(void *ctx)
{
    pcfg_status_t result=original_sync(ctx);
    ++sync_calls;
    if (sync_calls==corrupt_phase)
        save_flash_bytes[corrupt_phase==1 ? 8 : SAVE_SLOT_SIZE-4]^=1;
    return result;
}
static void test_accidental_header_and_commit_corruption_is_detected(void)
{
    static const uint8_t payload[]="verified";
    pcfg_store_t store;
    for(corrupt_phase=1;corrupt_phase<=2;++corrupt_phase)
    {
        size_t size=0;
        save_setup(); TEST_ASSERT(save_init(&store)==PCFG_OK);
        TEST_ASSERT(pcfg_load(&store,save_output,sizeof(save_output),&size,NULL,NULL)==PCFG_NOT_FOUND);
        original_sync=store.storage.sync;
        store.storage.sync=sync_with_corruption;
        sync_calls=0;
        TEST_ASSERT(pcfg_save(&store,payload,sizeof(payload)-1u,NULL,NULL)==PCFG_CORRUPT);
        TEST_ASSERT(pcfg_reload_required(&store));
    }
}
static void test_small_scratch_cold_save_with_larger_prior_payload(void)
{
    static uint32_t small_scratch[16];
    uint8_t payload[600];
    pcfg_store_t store;
    pcfg_config_t cfg=save_config();
    pcfg_storage_ops_t ops=pcfg_flash_storage_ops();
    memset(payload,0x27,sizeof(payload));
    save_setup(); TEST_ASSERT(save_init(&store)==PCFG_OK);
    TEST_ASSERT(pcfg_save(&store,payload,sizeof(payload),NULL,NULL)==PCFG_OK);
    TEST_ASSERT(pcfg_init(&store,&cfg,&ops,&save_flash,small_scratch,sizeof(small_scratch))==PCFG_OK);
    TEST_ASSERT(pcfg_save(&store,payload,sizeof(payload),NULL,NULL)==PCFG_OK);
}

int main(int argc,char **argv)
{
    if (argc>1)
    {
        if (strcmp(argv[1],"coldsmall")==0) test_small_scratch_cold_save_with_larger_prior_payload();
        if (strcmp(argv[1],"small")==0) test_small_scratch_save_streams_and_verifies();
        if (strcmp(argv[1],"verify")==0) test_save_verifies_header_and_final_commit();
        return 0;
    }

    RUN_TEST(test_first_save_and_round_trip);
    RUN_TEST(test_save_alternates_and_factory_reset_is_a_commit);
    RUN_TEST(test_validator_runs_before_storage_mutation);
    RUN_TEST(test_payload_limit_and_tail_padding);
    RUN_TEST(test_small_scratch_save_streams_and_verifies);
    RUN_TEST(test_save_verifies_header_and_final_commit);
    RUN_TEST(test_accidental_header_and_commit_corruption_is_detected);
    RUN_TEST(test_small_scratch_cold_save_with_larger_prior_payload);
    puts("test_save: PASS");
    return 0;
}
