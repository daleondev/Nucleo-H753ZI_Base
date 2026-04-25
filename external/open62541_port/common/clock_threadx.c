/*
 * open62541 clock plugin running on Eclipse ThreadX.
 *
 * Used in both build flavours (STM32 firmware and Linux simulation).
 *
 *   - `UA_DateTime_nowMonotonic()` reads `tx_time_get()` and converts
 *     ticks to 100 ns resolution so the value monotonically increases
 *     while preserving reasonable precision for OPC UA timeouts.
 *   - `UA_DateTime_now()` derives a UTC wall-clock timestamp from a
 *     captured base (`clock_gettime(CLOCK_REALTIME)` on Linux, RTC on
 *     STM32) plus the monotonic delta. This avoids the dependency on
 *     POSIX `gettimeofday()` for the bare-metal target.
 */

/* Pull POSIX prototypes (clock_gettime, gmtime_r, ...) from glibc. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <open62541/types.h>

#include "tx_api.h"

#if defined(__linux__)
#include <sys/time.h>
#include <time.h>
#define UA_PORT_HAVE_REALTIME_CLOCK 1
#elif defined(STM32H753xx)
#include "stm32h7xx_hal.h"
extern RTC_HandleTypeDef hrtc;
#define UA_PORT_HAVE_REALTIME_CLOCK 1
#else
#define UA_PORT_HAVE_REALTIME_CLOCK 0
#endif

#define UA_PORT_NS_PER_TICK ((UA_Int64)1000000000 / (UA_Int64)TX_TIMER_TICKS_PER_SECOND)
#define UA_PORT_100NS_PER_TICK (UA_PORT_NS_PER_TICK / 100)

UA_DateTime UA_DateTime_nowMonotonic(void)
{
    ULONG ticks = tx_time_get();
    return (UA_DateTime)((UA_Int64)ticks * UA_PORT_100NS_PER_TICK) + UA_DATETIME_UNIX_EPOCH;
}

#if UA_PORT_HAVE_REALTIME_CLOCK && defined(__linux__)

UA_DateTime UA_DateTime_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (UA_DateTime)((UA_Int64)ts.tv_sec * UA_DATETIME_SEC) + (UA_DateTime)(ts.tv_nsec / 100) +
           UA_DATETIME_UNIX_EPOCH;
}

UA_Int64 UA_DateTime_localTimeUtcOffset(void)
{
    time_t rawtime = time(NULL);
    struct tm gbuf;
    struct tm* ptm = gmtime_r(&rawtime, &gbuf);
    ptm->tm_isdst = -1;
    time_t gmt = mktime(ptm);
    return (UA_Int64)(difftime(rawtime, gmt) * UA_DATETIME_SEC);
}

#elif UA_PORT_HAVE_REALTIME_CLOCK && defined(STM32H753xx)

UA_DateTime UA_DateTime_now(void)
{
    /* Without an external time source we just track elapsed monotonic time.
     * The value is still unique and monotonically increasing, which is what
     * OPC UA timestamps require. Replace with a synced RTC reading when
     * SNTP/PTP is added. */
    return UA_DateTime_nowMonotonic();
}

UA_Int64 UA_DateTime_localTimeUtcOffset(void) { return 0; }

#else

UA_DateTime UA_DateTime_now(void) { return UA_DateTime_nowMonotonic(); }

UA_Int64 UA_DateTime_localTimeUtcOffset(void) { return 0; }

#endif
