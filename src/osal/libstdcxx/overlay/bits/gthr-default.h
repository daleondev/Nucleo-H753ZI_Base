#ifndef OSAL_GTHR_DEFAULT_H
#define OSAL_GTHR_DEFAULT_H

#include "osal/libstdcxx/backend.hpp"

#include <cerrno>

#if !defined(__ARM_EABI__) && !defined(__linux__)
#error "The ThreadX libstdc++ adapter is only supported for Arm EABI and Linux builds"
#endif

#if !defined(_GLIBCXX_RELEASE) || _GLIBCXX_RELEASE != 15
#error "The ThreadX libstdc++ adapter requires libstdc++ 15"
#endif

#if defined(__ARM_EABI__) && (!defined(__GLIBCXX__) || __GLIBCXX__ != 20251203)
#error "The ThreadX libstdc++ adapter requires Arm GNU Toolchain 15.2.Rel1 (libstdc++ 20251203)"
#endif

#define __GTHREADS 1
#define __GTHREADS_CXX0X 1
#define __GTHREAD_HAS_COND 1

typedef osal::detail::ThreadHandle __gthread_t;
typedef unsigned int __gthread_key_t;
typedef osal::detail::OnceHandle __gthread_once_t;
typedef osal::detail::MutexHandle __gthread_mutex_t;
typedef osal::detail::RecursiveMutexHandle __gthread_recursive_mutex_t;
typedef osal::detail::ConditionHandle __gthread_cond_t;
typedef osal::detail::TimePoint __gthread_time_t;

#define __GTHREAD_ONCE_INIT                                                                                  \
    {                                                                                                        \
    }
#define __GTHREAD_MUTEX_INIT_FUNCTION __gthread_mutex_init_function
#define __GTHREAD_RECURSIVE_MUTEX_INIT_FUNCTION __gthread_recursive_mutex_init_function
#define __GTHREAD_COND_INIT_FUNCTION __gthread_cond_init_function

inline int __gthread_active_p() { return osal::detail::active() ? 1 : 0; }

inline int __gthread_create(__gthread_t* thread, void* (*entry)(void*), void* argument)
{
    return osal::detail::thread_create(thread, entry, argument);
}

inline int __gthread_join(__gthread_t thread, void** result)
{
    return osal::detail::thread_join(thread, result);
}

inline int __gthread_detach(__gthread_t thread) { return osal::detail::thread_detach(thread); }

inline int __gthread_equal(__gthread_t lhs, __gthread_t rhs) { return lhs == rhs; }

inline __gthread_t __gthread_self() { return osal::detail::thread_self(); }

inline int __gthread_yield() { return osal::detail::thread_yield(); }

inline void __gthread_mutex_init_function(__gthread_mutex_t* mutex) { osal::detail::mutex_init(mutex); }

inline void __gthread_recursive_mutex_init_function(__gthread_recursive_mutex_t* mutex)
{
    osal::detail::recursive_mutex_init(mutex);
}

inline int __gthread_mutex_destroy(__gthread_mutex_t* mutex) { return osal::detail::mutex_destroy(mutex); }

inline int __gthread_recursive_mutex_destroy(__gthread_recursive_mutex_t* mutex)
{
    return osal::detail::recursive_mutex_destroy(mutex);
}

inline int __gthread_mutex_lock(__gthread_mutex_t* mutex) { return osal::detail::mutex_lock(mutex); }

inline int __gthread_recursive_mutex_lock(__gthread_recursive_mutex_t* mutex)
{
    return osal::detail::recursive_mutex_lock(mutex);
}

inline int __gthread_mutex_trylock(__gthread_mutex_t* mutex) { return osal::detail::mutex_try_lock(mutex); }

inline int __gthread_recursive_mutex_trylock(__gthread_recursive_mutex_t* mutex)
{
    return osal::detail::recursive_mutex_try_lock(mutex);
}

inline int __gthread_mutex_timedlock(__gthread_mutex_t* mutex, const __gthread_time_t* deadline)
{
    return osal::detail::mutex_timed_lock(mutex, deadline);
}

inline int __gthread_recursive_mutex_timedlock(__gthread_recursive_mutex_t* mutex,
                                               const __gthread_time_t* deadline)
{
    return osal::detail::recursive_mutex_timed_lock(mutex, deadline);
}

inline int __gthread_mutex_unlock(__gthread_mutex_t* mutex) { return osal::detail::mutex_unlock(mutex); }

inline int __gthread_recursive_mutex_unlock(__gthread_recursive_mutex_t* mutex)
{
    return osal::detail::recursive_mutex_unlock(mutex);
}

inline void __gthread_cond_init_function(__gthread_cond_t* condition)
{
    osal::detail::condition_init(condition);
}

inline int __gthread_cond_destroy(__gthread_cond_t* condition)
{
    return osal::detail::condition_destroy(condition);
}

inline int __gthread_cond_wait(__gthread_cond_t* condition, __gthread_mutex_t* mutex)
{
    return osal::detail::condition_wait(condition, mutex);
}

inline int __gthread_cond_wait_recursive(__gthread_cond_t*, __gthread_recursive_mutex_t*) { return ENOTSUP; }

inline int __gthread_cond_timedwait(__gthread_cond_t* condition,
                                    __gthread_mutex_t* mutex,
                                    const __gthread_time_t* deadline)
{
    return osal::detail::condition_timed_wait(condition, mutex, deadline);
}

inline int __gthread_cond_signal(__gthread_cond_t* condition)
{
    return osal::detail::condition_signal(condition);
}

inline int __gthread_cond_broadcast(__gthread_cond_t* condition)
{
    return osal::detail::condition_broadcast(condition);
}

inline int __gthread_once(__gthread_once_t* once_control, void (*function)())
{
    return osal::detail::once(once_control, function);
}

inline int __gthread_key_create(__gthread_key_t*, void (*)(void*)) { return ENOTSUP; }
inline int __gthread_key_delete(__gthread_key_t) { return ENOTSUP; }
inline void* __gthread_getspecific(__gthread_key_t) { return nullptr; }
inline int __gthread_setspecific(__gthread_key_t, const void*) { return ENOTSUP; }

#endif // OSAL_GTHR_DEFAULT_H
