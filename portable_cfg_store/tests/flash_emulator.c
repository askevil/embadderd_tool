#include "flash_emulator.h"

#include <limits.h>
#include <string.h>

static int range_valid(const pcfg_flash_t *flash, uint32_t address, size_t size)
{
    return (size <= flash->size) && ((size_t)address <= (flash->size - size));
}

static int fault_matches(const pcfg_flash_t *flash)
{
    return (flash->fault_enabled != 0) &&
           (flash->operation_count == flash->fault_operation);
}

static size_t fault_prefix(const pcfg_flash_t *flash, size_t size)
{
    return (flash->fault_after_bytes < size) ? flash->fault_after_bytes : size;
}

static pcfg_status_t flash_read(void *ctx, uint32_t address, void *dst,
                                size_t size)
{
    pcfg_flash_t *flash = (pcfg_flash_t *)ctx;
    size_t prefix;
    flash->operation_count++;
    if ((dst == NULL) || !range_valid(flash, address, size))
    {
        return PCFG_STORAGE_ERROR;
    }
    prefix = fault_matches(flash) ? fault_prefix(flash, size) : size;
    memcpy(dst, flash->bytes + address, prefix);
    if (fault_matches(flash))
    {
        flash->fault_enabled = 0;
        return PCFG_STORAGE_ERROR;
    }
    return PCFG_OK;
}

static pcfg_status_t flash_program(void *ctx, uint32_t address,
                                   const void *src, size_t size)
{
    pcfg_flash_t *flash = (pcfg_flash_t *)ctx;
    const uint8_t *input = (const uint8_t *)src;
    size_t index;
    size_t prefix;
    int injected;
    flash->operation_count++;

    if ((input == NULL) || !range_valid(flash, address, size) ||
        ((address % flash->program_unit) != 0u) ||
        ((size % flash->program_unit) != 0u) ||
        ((flash->page_size != 0u) &&
         (((address / flash->page_size) !=
           ((address + (uint32_t)size - 1u) / flash->page_size)))))
    {
        return PCFG_STORAGE_ERROR;
    }
    injected = fault_matches(flash);
    prefix = injected ? fault_prefix(flash, size) : size;
    for (index = 0u; index < prefix; ++index)
    {
        if ((flash->bytes[address + index] & input[index]) != input[index])
        {
            if (injected)
            {
                flash->fault_enabled = 0;
            }
            return PCFG_STORAGE_ERROR;
        }
    }
    for (index = 0u; index < prefix; ++index)
    {
        flash->bytes[address + index] &= input[index];
    }
    if (prefix != 0u)
    {
        flash->mutation_count++;
    }
    if (injected)
    {
        flash->fault_enabled = 0;
        return PCFG_STORAGE_ERROR;
    }
    return PCFG_OK;
}

static pcfg_status_t flash_erase(void *ctx, uint32_t address, size_t size)
{
    pcfg_flash_t *flash = (pcfg_flash_t *)ctx;
    size_t prefix;
    int injected;
    flash->operation_count++;
    if (!range_valid(flash, address, size) ||
        ((address % flash->erase_unit) != 0u) ||
        ((size % flash->erase_unit) != 0u))
    {
        return PCFG_STORAGE_ERROR;
    }
    injected = fault_matches(flash);
    prefix = injected ? fault_prefix(flash, size) : size;
    memset(flash->bytes + address, 0xff, prefix);
    if (prefix != 0u)
    {
        flash->mutation_count++;
    }
    if (injected)
    {
        flash->fault_enabled = 0;
        return PCFG_STORAGE_ERROR;
    }
    return PCFG_OK;
}

static pcfg_status_t flash_sync(void *ctx)
{
    pcfg_flash_t *flash = (pcfg_flash_t *)ctx;
    flash->operation_count++;
    if (fault_matches(flash))
    {
        flash->fault_enabled = 0;
        return PCFG_STORAGE_ERROR;
    }
    return PCFG_OK;
}

void pcfg_flash_init(pcfg_flash_t *flash, uint8_t *bytes, size_t size,
                     size_t erase_unit, size_t program_unit,
                     size_t page_size, int erase_contents)
{
    memset(flash, 0, sizeof(*flash));
    flash->bytes = bytes;
    flash->size = size;
    flash->erase_unit = erase_unit;
    flash->program_unit = program_unit;
    flash->page_size = page_size;
    flash->fault_operation = SIZE_MAX;
    flash->fault_after_bytes = SIZE_MAX;
    if (erase_contents != 0)
    {
        memset(bytes, 0xff, size);
    }
}

void pcfg_flash_reset_faults(pcfg_flash_t *flash)
{
    flash->operation_count = 0u;
    flash->fault_enabled = 0;
    flash->fault_operation = SIZE_MAX;
    flash->fault_after_bytes = SIZE_MAX;
    flash->fault_may_persist = 0;
}

void pcfg_flash_fail_operation(pcfg_flash_t *flash, size_t operation_index,
                               size_t after_bytes, int may_persist)
{
    flash->fault_enabled = 1;
    flash->fault_operation = operation_index;
    flash->fault_after_bytes = after_bytes;
    flash->fault_may_persist = may_persist;
}

void pcfg_flash_corrupt(pcfg_flash_t *flash, size_t address, uint8_t mask)
{
    if (address < flash->size)
    {
        flash->bytes[address] ^= mask;
    }
}

pcfg_storage_ops_t pcfg_flash_storage_ops(void)
{
    pcfg_storage_ops_t operations;
    operations.read = flash_read;
    operations.program = flash_program;
    operations.erase = flash_erase;
    operations.sync = flash_sync;
    return operations;
}
