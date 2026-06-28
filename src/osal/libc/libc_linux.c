#include "libc.h"

int osal_init_libc(void)
{
    /* The ThreadX Linux port represents every simulated ThreadX thread with a
     * pthread. glibc therefore supplies both internal locking and per-pthread
     * libc state without Newlib's retargeting hooks. */
    return 0;
}
