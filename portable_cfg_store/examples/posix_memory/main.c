#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pcfg_store.h"

#define FLASH_SIZE 8192u
#define SLOT_SIZE 4096u

static unsigned char flash_bytes[FLASH_SIZE];
static unsigned char scratch[SLOT_SIZE];
static unsigned char output[SLOT_SIZE];

static pcfg_status_t memory_read(void *ctx, uint32_t address, void *dst,
                                 size_t size)
{
    (void)ctx;
    if ((dst == NULL) || (size > FLASH_SIZE) ||
        ((size_t)address > FLASH_SIZE - size))
    {
        return PCFG_STORAGE_ERROR;
    }
    memcpy(dst, flash_bytes + address, size);
    return PCFG_OK;
}

static pcfg_status_t memory_program(void *ctx, uint32_t address,
                                    const void *src, size_t size)
{
    const unsigned char *input = (const unsigned char *)src;
    size_t index;
    (void)ctx;
    if ((input == NULL) || (size > FLASH_SIZE) ||
        ((size_t)address > FLASH_SIZE - size))
    {
        return PCFG_STORAGE_ERROR;
    }
    for (index = 0u; index < size; ++index)
    {
        if ((flash_bytes[address + index] & input[index]) != input[index])
        {
            return PCFG_STORAGE_ERROR;
        }
    }
    for (index = 0u; index < size; ++index)
    {
        flash_bytes[address + index] &= input[index];
    }
    return PCFG_OK;
}

static pcfg_status_t memory_erase(void *ctx, uint32_t address, size_t size)
{
    (void)ctx;
    if ((size > FLASH_SIZE) || ((size_t)address > FLASH_SIZE - size) ||
        ((address % 256u) != 0u) || ((size % 256u) != 0u))
    {
        return PCFG_STORAGE_ERROR;
    }
    memset(flash_bytes + address, 0xff, size);
    return PCFG_OK;
}

static pcfg_status_t memory_sync(void *ctx)
{
    (void)ctx;
    return PCFG_OK;
}

static pcfg_storage_ops_t memory_ops(void)
{
    pcfg_storage_ops_t ops;
    ops.read = memory_read;
    ops.program = memory_program;
    ops.erase = memory_erase;
    ops.sync = memory_sync;
    return ops;
}

static void die_if(pcfg_status_t status, const char *operation)
{
    if (status != PCFG_OK)
    {
        fprintf(stderr, "%s failed: %d\n", operation, (int)status);
        fflush(stderr);
        exit(1);
    }
}

int main(void)
{
    static const unsigned char first[] = "{\"brightness\":42}";
    static const unsigned char defaults[] = "{\"brightness\":10}";
    pcfg_config_t config;
    pcfg_store_t store;
    pcfg_storage_ops_t ops = memory_ops();
    size_t output_size = 0u;
    pcfg_status_t status;

    memset(flash_bytes, 0xff, sizeof(flash_bytes));
    memset(&config, 0, sizeof(config));
    config.slot_address[0] = 0u;
    config.slot_address[1] = SLOT_SIZE;
    config.slot_size = SLOT_SIZE;
    config.erase_unit = 256u;
    config.program_unit = 4u;
    config.program_page_size = 256u;
    config.io_chunk_size = 64u;
    config.format_magic = UINT32_C(0x50434631);

    /*
     * In an RTOS application, hold one mutex from before this call until the
     * complete load/save/reset operation returns. The callbacks may have a
     * separate device lock, but that does not replace the transaction lock.
     */
    die_if(pcfg_init(&store, &config, &ops, NULL, scratch, sizeof(scratch)),
           "init");
    die_if(pcfg_save(&store, first, sizeof(first) - 1u, NULL, NULL), "save");
    status = pcfg_load(&store, output, sizeof(output), &output_size, NULL, NULL);
    die_if(status, "load");
    printf("loaded: brightness=42\n");
    printf("sequence: %u\n", (unsigned)pcfg_active_sequence(&store));

    die_if(pcfg_factory_reset(&store, defaults, sizeof(defaults) - 1u,
                              NULL, NULL), "factory reset");
    status = pcfg_load(&store, output, sizeof(output), &output_size, NULL, NULL);
    die_if(status, "load default");
    printf("loaded default: brightness=10\n");
    return 0;
}
