#include "pcfg_crc32.h"

uint32_t pcfg_crc32_init(void)
{
    return UINT32_C(0xffffffff);
}

uint32_t pcfg_crc32_update(uint32_t state, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    if ((bytes == NULL) && (size != 0u))
    {
        return state;
    }

    for (index = 0u; index < size; ++index)
    {
        unsigned int bit;
        state ^= bytes[index];
        for (bit = 0u; bit < 8u; ++bit)
        {
            uint32_t mask = (uint32_t)(0u - (state & UINT32_C(1)));
            state = (state >> 1u) ^ (UINT32_C(0xedb88320) & mask);
        }
    }

    return state;
}

uint32_t pcfg_crc32_finish(uint32_t state)
{
    return state ^ UINT32_C(0xffffffff);
}

uint32_t pcfg_crc32(const void *data, size_t size)
{
    return pcfg_crc32_finish(pcfg_crc32_update(pcfg_crc32_init(), data, size));
}
