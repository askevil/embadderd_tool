#include "pcfg_rtthread_device.h"
#include <limits.h>

static bool address_valid(uint32_t address, size_t size)
{
    rt_off_t converted = (rt_off_t)address;
    return (converted >= 0) && ((uint64_t)converted == address) &&
           (size <= UINT32_MAX) && (address <= UINT32_MAX - (uint32_t)size) &&
           ((rt_size_t)size == size);
}
static pcfg_status_t device_read(void *context, uint32_t address, void *buffer, size_t size)
{
    pcfg_rtthread_device_t *adapter = (pcfg_rtthread_device_t *)context;
    if ((adapter == NULL) || (adapter->device == NULL) || (buffer == NULL) ||
        !address_valid(address,size)) return PCFG_INVALID_ARGUMENT;
    return rt_device_read(adapter->device,(rt_off_t)address,buffer,(rt_size_t)size) == size ?
           PCFG_OK : PCFG_STORAGE_ERROR;
}
static pcfg_status_t device_program(void *context, uint32_t address, const void *buffer, size_t size)
{
    pcfg_rtthread_device_t *adapter = (pcfg_rtthread_device_t *)context;
    if ((adapter == NULL) || (adapter->device == NULL) || (buffer == NULL) ||
        !address_valid(address,size)) return PCFG_INVALID_ARGUMENT;
    return rt_device_write(adapter->device,(rt_off_t)address,buffer,(rt_size_t)size) == size ?
           PCFG_OK : PCFG_STORAGE_ERROR;
}
static pcfg_status_t device_erase(void *context, uint32_t address, size_t size)
{
    pcfg_rtthread_device_t *adapter = (pcfg_rtthread_device_t *)context;
    if ((adapter == NULL) || (adapter->erase == NULL) || !address_valid(address,size))
        return PCFG_INVALID_ARGUMENT;
    return adapter->erase(adapter->context,address,size);
}
static pcfg_status_t device_sync(void *context)
{
    pcfg_rtthread_device_t *adapter = (pcfg_rtthread_device_t *)context;
    if (adapter == NULL) return PCFG_INVALID_ARGUMENT;
    return adapter->sync != NULL ? adapter->sync(adapter->context) : PCFG_OK;
}
pcfg_storage_ops_t pcfg_rtthread_device_ops(void)
{
    pcfg_storage_ops_t ops = {device_read,device_program,device_erase,device_sync};
    return ops;
}
