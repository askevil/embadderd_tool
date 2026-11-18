#ifndef PCFG_FLASH_EMULATOR_H
#define PCFG_FLASH_EMULATOR_H

#include <stddef.h>
#include <stdint.h>

#include "pcfg_store.h"

typedef enum
{
    PCFG_FLASH_OP_READ = 0,
    PCFG_FLASH_OP_PROGRAM,
    PCFG_FLASH_OP_ERASE,
    PCFG_FLASH_OP_SYNC
} pcfg_flash_operation_t;

typedef struct
{
    uint8_t *bytes;
    size_t size;
    size_t erase_unit;
    size_t program_unit;
    size_t page_size;
    size_t operation_count;
    size_t mutation_count;
    int fault_enabled;
    size_t fault_operation;
    size_t fault_after_bytes;
    int fault_may_persist;
} pcfg_flash_t;

void pcfg_flash_init(pcfg_flash_t *flash, uint8_t *bytes, size_t size,
                     size_t erase_unit, size_t program_unit,
                     size_t page_size, int erase_contents);
void pcfg_flash_reset_faults(pcfg_flash_t *flash);
void pcfg_flash_fail_operation(pcfg_flash_t *flash, size_t operation_index,
                               size_t after_bytes, int may_persist);
void pcfg_flash_corrupt(pcfg_flash_t *flash, size_t address, uint8_t mask);
pcfg_storage_ops_t pcfg_flash_storage_ops(void);

#endif
