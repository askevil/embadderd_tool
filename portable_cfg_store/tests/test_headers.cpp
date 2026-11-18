#include "pcfg_store.h"
#include "pcfg_crc32.h"
#ifdef PCFG_TEST_CJSON
#include "pcfg_cjson.h"
#endif
#ifdef PCFG_TEST_RTTHREAD
#include "pcfg_rtthread_device.h"
#endif
int main()
{
    pcfg_store_t store = {};
    return pcfg_active_slot(&store) == 0 ? 0 : 1;
}
