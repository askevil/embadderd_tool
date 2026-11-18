#ifndef PCFG_RTTHREAD_DEVICE_H
#define PCFG_RTTHREAD_DEVICE_H
#include <rtthread.h>
#include <rtdevice.h>
#include "pcfg_store.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Requires a byte-addressed device: positions and counts are bytes, not blocks.
 * The application owns device open/close, geometry, and the transaction lock. */
typedef struct
{
    rt_device_t device;
    void *context;
    pcfg_erase_fn erase;
    pcfg_sync_fn sync;
} pcfg_rtthread_device_t;
pcfg_storage_ops_t pcfg_rtthread_device_ops(void);
#ifdef __cplusplus
}
#endif
#endif
