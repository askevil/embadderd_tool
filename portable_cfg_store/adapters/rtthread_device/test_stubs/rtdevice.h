#ifndef PCFG_TEST_RTDEVICE_H
#define PCFG_TEST_RTDEVICE_H
#include <rtthread.h>
#ifdef __cplusplus
extern "C" {
#endif
struct rt_device;
typedef struct rt_device *rt_device_t;
rt_size_t rt_device_read(rt_device_t device, rt_off_t position, void *buffer, rt_size_t size);
rt_size_t rt_device_write(rt_device_t device, rt_off_t position, const void *buffer, rt_size_t size);
#ifdef __cplusplus
}
#endif
#endif
