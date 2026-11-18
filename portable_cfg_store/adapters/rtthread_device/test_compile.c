#include <stdint.h>
#include "pcfg_rtthread_device.h"
#include "test_harness.h"
struct rt_device { int unused; };
static struct rt_device fake;
static int short_io;
static int erased;
rt_size_t rt_device_read(rt_device_t device, rt_off_t position, void *buffer, rt_size_t size)
{
    TEST_ASSERT(device == &fake); TEST_ASSERT(position == 128); (void)buffer;
    return short_io ? size - 1u : size;
}
rt_size_t rt_device_write(rt_device_t device, rt_off_t position, const void *buffer, rt_size_t size)
{
    TEST_ASSERT(device == &fake); TEST_ASSERT(position == 128); (void)buffer;
    return short_io ? size - 1u : size;
}
static pcfg_status_t erase_callback(void *context, uint32_t address, size_t size)
{
    TEST_ASSERT(context == &fake); TEST_ASSERT(address == 128); TEST_ASSERT(size == 16);
    erased++; return PCFG_OK;
}
int main(void)
{
    pcfg_rtthread_device_t adapter = {&fake, &fake, erase_callback, NULL};
    pcfg_storage_ops_t ops = pcfg_rtthread_device_ops();
    uint8_t bytes[16];
    TEST_ASSERT(ops.read(&adapter,128,bytes,sizeof(bytes)) == PCFG_OK);
    TEST_ASSERT(ops.program(&adapter,128,bytes,sizeof(bytes)) == PCFG_OK);
    short_io = 1;
    TEST_ASSERT(ops.read(&adapter,128,bytes,sizeof(bytes)) == PCFG_STORAGE_ERROR);
    TEST_ASSERT(ops.program(&adapter,128,bytes,sizeof(bytes)) == PCFG_STORAGE_ERROR);
    TEST_ASSERT(ops.erase(&adapter,128,16) == PCFG_OK); TEST_ASSERT(erased == 1);
    TEST_ASSERT(ops.sync(&adapter) == PCFG_OK);
    TEST_ASSERT(ops.read(&adapter,UINT32_MAX,bytes,16) == PCFG_INVALID_ARGUMENT);
    puts("test_rtthread_device: PASS");
    return 0;
}
