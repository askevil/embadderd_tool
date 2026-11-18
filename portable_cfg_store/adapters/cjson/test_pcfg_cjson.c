#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "flash_emulator.h"

#include <cJSON.h>

#include "pcfg_cjson.h"
#include "test_harness.h"

static pcfg_status_t validate_document(void *ctx, const cJSON *root)
{
    const cJSON *enabled;
    (void)ctx;
    enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    return cJSON_IsBool(enabled) ? PCFG_OK : PCFG_VALIDATION_FAILED;
}

static void test_loads_json_and_gets_typed_values(void)
{
    static const uint8_t stored[] = "{\"enabled\":true,\"count\":-7,\"name\":\"ok\",\"ratio\":1.25}";
    pcfg_cjson_document_t document;
    char name[8];
    int32_t count = 0;
    double ratio = 0.0;
    bool enabled = false;
    size_t required = 0u;

    TEST_ASSERT(pcfg_cjson_init(&document, NULL, 0u, NULL, NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_load_bytes(&document, stored, sizeof(stored) - 1u,
                                      validate_document, NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_get_bool(&document, "enabled", &enabled) == PCFG_OK);
    TEST_ASSERT(enabled == true);
    TEST_ASSERT(pcfg_cjson_get_int32(&document, "count", &count) == PCFG_OK);
    TEST_ASSERT(count == -7);
    TEST_ASSERT(pcfg_cjson_get_double(&document, "ratio", &ratio) == PCFG_OK);
    TEST_ASSERT(ratio == 1.25);
    TEST_ASSERT(pcfg_cjson_get_string(&document, "name", name, sizeof(name)) == PCFG_OK);
    TEST_ASSERT(strcmp(name, "ok") == 0);
    TEST_ASSERT(pcfg_cjson_serialize(&document, NULL, 0u, &required) ==
                PCFG_BUFFER_TOO_SMALL);
    TEST_ASSERT(required > 0u);
    pcfg_cjson_destroy(&document);
}

static void test_rejects_malformed_json_and_implicit_type_replacement(void)
{
    static const uint8_t malformed[] = "{\"enabled\":";
    static const uint8_t valid[] = "{\"enabled\":true}";
    pcfg_cjson_document_t document;
    bool value;

    TEST_ASSERT(pcfg_cjson_init(&document, NULL, 0u, NULL, NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_load_bytes(&document, malformed,
                                      sizeof(malformed) - 1u,
                                      NULL, NULL) == PCFG_CORRUPT);
    TEST_ASSERT(pcfg_cjson_load_bytes(&document, valid, sizeof(valid) - 1u,
                                      NULL, NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_set_int32(&document, "enabled", 1) ==
                PCFG_VALIDATION_FAILED);
    TEST_ASSERT(pcfg_cjson_get_bool(&document, "enabled", &value) == PCFG_OK);
    TEST_ASSERT(value == true);
    pcfg_cjson_destroy(&document);
}

static void test_set_creates_missing_objects_and_tracks_dirty(void)
{
    pcfg_cjson_document_t document;
    int32_t value = 0;
    bool dirty = true;

    TEST_ASSERT(pcfg_cjson_init(&document, NULL, 0u, NULL, NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_load_bytes(&document, (const uint8_t *)"{}", 2u,
                                      NULL, NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_is_dirty(&document) == false);
    TEST_ASSERT(pcfg_cjson_set_int32(&document, "network.timeout", 30) ==
                PCFG_OK);
    TEST_ASSERT(pcfg_cjson_is_dirty(&document) == true);
    TEST_ASSERT(pcfg_cjson_get_int32(&document, "network.timeout", &value) ==
                PCFG_OK);
    TEST_ASSERT(value == 30);
    TEST_ASSERT(pcfg_cjson_clear_dirty(&document) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_is_dirty(&document) == false);
    TEST_ASSERT(pcfg_cjson_get_bool(&document, "network.timeout", &dirty) ==
                PCFG_VALIDATION_FAILED);
    pcfg_cjson_destroy(&document);
}

static void test_rejects_duplicate_keys_trailing_data_and_small_string_buffer(void)
{
    static const uint8_t duplicate[] = "{\"value\":1,\"value\":2}";
    static const uint8_t trailing[] = "{\"value\":1} garbage";
    static const uint8_t valid[] = "{\"name\":\"long\"}";
    pcfg_cjson_document_t document;
    char value[4];

    TEST_ASSERT(pcfg_cjson_init(&document, NULL, 0u, NULL, NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_load_bytes(&document, duplicate,
                                      sizeof(duplicate) - 1u,
                                      NULL, NULL) == PCFG_VALIDATION_FAILED);
    TEST_ASSERT(pcfg_cjson_load_bytes(&document, trailing,
                                      sizeof(trailing) - 1u,
                                      NULL, NULL) == PCFG_CORRUPT);
    TEST_ASSERT(pcfg_cjson_load_bytes(&document, valid, sizeof(valid) - 1u,
                                      NULL, NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_get_string(&document, "name", value,
                                      sizeof(value)) == PCFG_BUFFER_TOO_SMALL);
    pcfg_cjson_destroy(&document);
}


static void test_rejects_arrays_dotted_keys_and_invalid_numbers(void)
{
    static const uint8_t array[] = "{\"items\":[1,2]}";
    static const uint8_t dotted_key[] = "{\"a.b\":1}";
    static const uint8_t valid[] = "{\"value\":0}";
    pcfg_cjson_document_t document;
    int32_t value;

    TEST_ASSERT(pcfg_cjson_init(&document, NULL, 0u, NULL, NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_load_bytes(&document, array, sizeof(array) - 1u,
                                      NULL, NULL) == PCFG_VALIDATION_FAILED);
    TEST_ASSERT(pcfg_cjson_load_bytes(&document, dotted_key,
                                      sizeof(dotted_key) - 1u,
                                      NULL, NULL) == PCFG_VALIDATION_FAILED);
    TEST_ASSERT(pcfg_cjson_load_bytes(&document, valid, sizeof(valid) - 1u,
                                      NULL, NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_set_int32(&document, "value", INT32_MAX) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_get_int32(&document, "value", &value) == PCFG_OK);
    TEST_ASSERT(value == INT32_MAX);
    TEST_ASSERT(pcfg_cjson_set_int32(&document, "value", INT32_MIN) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_set_double(&document, "value", NAN) ==
                PCFG_VALIDATION_FAILED);
    pcfg_cjson_destroy(&document);
}

static void *fail_allocation(size_t size)
{
    (void)size;
    return NULL;
}

static void test_oom_set_preserves_clean_document(void)
{
    pcfg_cjson_document_t document;
    cJSON_Hooks hooks;
    char serialized[64];
    size_t required = 0u;
    pcfg_status_t status;
    TEST_ASSERT(pcfg_cjson_init(&document, NULL, 0u, NULL, NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_load_bytes(&document, (const uint8_t *)"{}", 2u,
                                      NULL, NULL) == PCFG_OK);
    hooks.malloc_fn = fail_allocation;
    hooks.free_fn = free;
    cJSON_InitHooks(&hooks);
    status = pcfg_cjson_set_int32(&document, "missing", 42);
    cJSON_InitHooks(NULL);
    TEST_ASSERT(status != PCFG_OK);
    TEST_ASSERT(pcfg_cjson_is_dirty(&document) == false);
    TEST_ASSERT(pcfg_cjson_serialize(&document, serialized, sizeof(serialized),
                                     &required) == PCFG_OK);
    TEST_ASSERT(strcmp(serialized, "{}") == 0);
    pcfg_cjson_destroy(&document);
}

static void test_deep_path_exact_capacity_defaults_and_numeric_bounds(void)
{
    static const uint8_t defaults[] = "{\"value\":4}";
    pcfg_cjson_document_t document;
    int32_t value = 0;
    char serialized[128];
    size_t required = 0u;
    TEST_ASSERT(pcfg_cjson_init(&document, defaults, sizeof(defaults) - 1u,
                                 NULL, NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_load_default(&document) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_is_dirty(&document) == false);
    TEST_ASSERT(pcfg_cjson_set_int32(&document, "one.two.three", 7) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_get_int32(&document, "one.two.three", &value) == PCFG_OK);
    TEST_ASSERT(value == 7);
    TEST_ASSERT(pcfg_cjson_set_double(&document, "value", 1.25) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_get_int32(&document, "value", &value) == PCFG_VALIDATION_FAILED);
    TEST_ASSERT(pcfg_cjson_set_double(&document, "value", 2147483648.0) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_get_int32(&document, "value", &value) == PCFG_VALIDATION_FAILED);
    TEST_ASSERT(pcfg_cjson_set_double(&document, "value", INFINITY) == PCFG_VALIDATION_FAILED);
    TEST_ASSERT(pcfg_cjson_serialize(&document, NULL, 0u, &required) == PCFG_BUFFER_TOO_SMALL);
    TEST_ASSERT(required + 1u < sizeof(serialized));
    TEST_ASSERT(pcfg_cjson_serialize(&document, serialized, required, &required) == PCFG_BUFFER_TOO_SMALL);
    TEST_ASSERT(pcfg_cjson_serialize(&document, serialized, required + 1u, &required) == PCFG_OK);
    pcfg_cjson_destroy(&document);
}

static void test_storage_fallback_dirty_save_and_reset_failure(void)
{
    static uint8_t flash_bytes[8192];
    static uint32_t scratch[1024];
    uint8_t load_buffer[4096];
    char serialized[256];
    static const uint8_t defaults[] = "{\"value\":4}";
    static const uint8_t good[] = "{\"value\":9}";
    static const uint8_t bad[] = "{broken";
    pcfg_flash_t flash;
    pcfg_storage_ops_t ops = pcfg_flash_storage_ops();
    pcfg_config_t cfg = {{0,4096},4096,256,4,256,64,0x50434631u};
    pcfg_store_t store;
    pcfg_cjson_document_t document;
    int32_t value = 0;
    size_t mutations;
    pcfg_flash_init(&flash, flash_bytes, sizeof(flash_bytes), 256,4,256,1);
    TEST_ASSERT(pcfg_init(&store,&cfg,&ops,&flash,scratch,sizeof(scratch)) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_init(&document,defaults,sizeof(defaults)-1u,NULL,NULL) == PCFG_OK);
    mutations = flash.mutation_count;
    TEST_ASSERT(pcfg_cjson_load(&document,&store,load_buffer,sizeof(load_buffer)) == PCFG_OK);
    TEST_ASSERT(flash.mutation_count == mutations);
    TEST_ASSERT(pcfg_save(&store,good,sizeof(good)-1u,NULL,NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_save(&store,bad,sizeof(bad)-1u,NULL,NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_load(&document,&store,load_buffer,sizeof(load_buffer)) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_get_int32(&document,"value",&value) == PCFG_OK);
    TEST_ASSERT(value == 9);
    TEST_ASSERT(pcfg_cjson_set_int32(&document,"value",20) == PCFG_OK);
    pcfg_flash_reset_faults(&flash);
    pcfg_flash_fail_operation(&flash,1,0,1);
    TEST_ASSERT(pcfg_cjson_save(&document,&store,serialized,sizeof(serialized)) != PCFG_OK);
    TEST_ASSERT(pcfg_cjson_is_dirty(&document));
    TEST_ASSERT(pcfg_cjson_load(&document,&store,load_buffer,sizeof(load_buffer)) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_set_int32(&document,"value",20) == PCFG_OK);
    pcfg_flash_reset_faults(&flash);
    pcfg_flash_fail_operation(&flash,1,0,1);
    TEST_ASSERT(pcfg_cjson_factory_reset(&document,&store) != PCFG_OK);
    TEST_ASSERT(pcfg_cjson_get_int32(&document,"value",&value) == PCFG_OK);
    TEST_ASSERT(value == 20);
    TEST_ASSERT(pcfg_cjson_is_dirty(&document));
    pcfg_cjson_destroy(&document);
}

static void test_invalid_defaults_and_embedded_nul_are_rejected(void)
{
    pcfg_cjson_document_t document;
    static const uint8_t malformed[] = "{bad";
    static const uint8_t nul_data[] = {'{','}',0,'x'};
    TEST_ASSERT(pcfg_cjson_init(&document,malformed,sizeof(malformed)-1u,NULL,NULL) != PCFG_OK);
    pcfg_cjson_destroy(&document);
    TEST_ASSERT(pcfg_cjson_init(&document,NULL,0,NULL,NULL) == PCFG_OK);
    TEST_ASSERT(pcfg_cjson_load_bytes(&document,nul_data,sizeof(nul_data),NULL,NULL) != PCFG_OK);
    pcfg_cjson_destroy(&document);
}

int main(void)
{
    RUN_TEST(test_loads_json_and_gets_typed_values);
    RUN_TEST(test_rejects_malformed_json_and_implicit_type_replacement);
    RUN_TEST(test_set_creates_missing_objects_and_tracks_dirty);
    RUN_TEST(test_rejects_arrays_dotted_keys_and_invalid_numbers);
    RUN_TEST(test_rejects_duplicate_keys_trailing_data_and_small_string_buffer);
    RUN_TEST(test_oom_set_preserves_clean_document);
    RUN_TEST(test_deep_path_exact_capacity_defaults_and_numeric_bounds);
    RUN_TEST(test_storage_fallback_dirty_save_and_reset_failure);
    RUN_TEST(test_invalid_defaults_and_embedded_nul_are_rejected);
    puts("test_pcfg_cjson: PASS");
    return 0;
}
