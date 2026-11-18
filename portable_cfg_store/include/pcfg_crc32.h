#ifndef PCFG_CRC32_H
#define PCFG_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t pcfg_crc32_init(void);
uint32_t pcfg_crc32_update(uint32_t state, const void *data, size_t size);
uint32_t pcfg_crc32_finish(uint32_t state);
uint32_t pcfg_crc32(const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
