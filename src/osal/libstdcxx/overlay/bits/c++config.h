#ifndef OSAL_LIBSTDCXX_CXXCONFIG_H
#define OSAL_LIBSTDCXX_CXXCONFIG_H

#include_next <bits/c++config.h>

#if defined(THREADX_STD_ENABLED)

/* The Linux libstdc++ configuration enables pthread and futex fast paths
 * that bypass gthreads. They are incompatible with the ThreadX handle types
 * supplied by this overlay, so force the generic gthread implementations. */
#undef _GLIBCXX_HAVE_LINUX_FUTEX
#undef _GLIBCXX_HAVE_LINUX_FUTEX_PRIVATE
#undef _GLIBCXX_NATIVE_THREAD_ID
#undef _GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT
#undef _GLIBCXX_USE_PTHREAD_RWLOCK_CLOCKLOCK

#undef _GLIBCXX_USE_PTHREAD_MUTEX_CLOCKLOCK
#define _GLIBCXX_USE_PTHREAD_MUTEX_CLOCKLOCK 0

#undef _GLIBCXX_USE_PTHREAD_RWLOCK_T
#define _GLIBCXX_USE_PTHREAD_RWLOCK_T 0

#endif /* THREADX_STD_ENABLED */

#endif /* OSAL_LIBSTDCXX_CXXCONFIG_H */
