#include "platform.hpp"

// #include <sys/lock.h>

// #include <tx_api.h>

// #include <cstdint>

// /* Newlib deliberately keeps struct __lock opaque and defines its built-in
//  * lock objects as one-byte sentinels. All lock identities therefore share a
//  * separate ThreadX mutex instead of storing RTOS state inside Newlib memory. */
// struct __lock
// {
//     std::uint8_t unused;
// };

// namespace
// {
//     using CxxGuard = std::uint32_t;

//     static_assert(sizeof(CxxGuard) == 4U, "Arm EABI requires a 32-bit C++ guard");

//     constexpr CxxGuard CXX_GUARD_INITIALIZED{ 1U };
//     constexpr CxxGuard CXX_GUARD_IN_PROGRESS{ 1U << 8U };

//     TX_MUTEX libc_mutex;
//     CHAR libc_mutex_name[] = "Newlib libc";
//     bool libc_mutex_ready{};
//     __lock dynamic_lock_sentinel{};

//     TX_MUTEX cxx_guard_mutex;
//     CHAR cxx_guard_mutex_name[] = "C++ static init";
//     bool cxx_guard_mutex_ready{};

//     [[noreturn]] void libc_lock_failure()
//     {
//         Error_Handler();
//         while (true) {
//         }
//     }

//     bool is_thread_context()
//     {
//         if (__get_IPSR() != 0U) {
//             /* Newlib calls may block and are forbidden from interrupt context. */
//             libc_lock_failure();
//         }

//         const auto* current_thread = tx_thread_identify();

//         if (current_thread == TX_NULL) {
//             /* Before the scheduler starts, execution is single-threaded. */
//             return false;
//         }

//         if (!libc_mutex_ready) {
//             libc_lock_failure();
//         }

//         return true;
//     }

//     void acquire_libc_lock(ULONG wait_option)
//     {
//         if (is_thread_context() && tx_mutex_get(&libc_mutex, wait_option) != TX_SUCCESS) {
//             libc_lock_failure();
//         }
//     }

//     int try_acquire_libc_lock()
//     {
//         if (!is_thread_context()) {
//             return 1;
//         }

//         return tx_mutex_get(&libc_mutex, TX_NO_WAIT) == TX_SUCCESS ? 1 : 0;
//     }

//     void release_libc_lock()
//     {
//         if (is_thread_context() && tx_mutex_put(&libc_mutex) != TX_SUCCESS) {
//             libc_lock_failure();
//         }
//     }

//     bool is_cxx_guard_thread_context()
//     {
//         if (__get_IPSR() != 0U) {
//             /* Function-local static initialization may block and is forbidden
//              * from interrupt context. */
//             libc_lock_failure();
//         }

//         if (tx_thread_identify() == TX_NULL) {
//             /* Before the scheduler starts, execution is single-threaded. */
//             return false;
//         }

//         if (!cxx_guard_mutex_ready) {
//             libc_lock_failure();
//         }

//         return true;
//     }

//     CxxGuard load_cxx_guard(const CxxGuard* guard) { return __atomic_load_n(guard, __ATOMIC_ACQUIRE); }

//     void store_cxx_guard(CxxGuard* guard, CxxGuard value)
//     {
//         __atomic_store_n(guard, value, __ATOMIC_RELEASE);
//     }
// }

namespace
{
    constexpr uint32_t NANOSECONDS_PER_SECOND{ 1'000'000'000U };

    [[nodiscard]] bool is_leap_year(int32_t year)
    {
        return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    }

    [[nodiscard]] uint32_t days_in_month(int32_t year, uint32_t month)
    {
        constexpr uint8_t days_per_month[]{ 31U, 28U, 31U, 30U, 31U, 30U,
                                            31U, 31U, 30U, 31U, 30U, 31U };
        if (month == 0U || month > 12U) {
            return 0U;
        }
        if (month == 2U && is_leap_year(year)) {
            return 29U;
        }
        return days_per_month[month - 1U];
    }

    [[nodiscard]] int64_t days_from_civil(int32_t year, uint32_t month, uint32_t day)
    {
        year -= month <= 2U ? 1 : 0;
        const int32_t era{ (year >= 0 ? year : year - 399) / 400 };
        const uint32_t year_of_era{ static_cast<uint32_t>(year - era * 400) };
        const uint32_t adjusted_month{ month > 2U ? month - 3U : month + 9U };
        const uint32_t day_of_year{
            (153U * adjusted_month + 2U) / 5U + day - 1U
        };
        const uint32_t day_of_era{
            year_of_era * 365U + year_of_era / 4U - year_of_era / 100U + day_of_year
        };
        return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(day_of_era) - 719468;
    }
}

extern "C" HAL_StatusTypeDef platform_get_system_time(int64_t* seconds_since_epoch,
                                                       uint32_t* nanoseconds)
{
    if (seconds_since_epoch == nullptr || nanoseconds == nullptr) {
        return HAL_ERROR;
    }

    RTC_TimeTypeDef time{};
    RTC_DateTypeDef date{};
    if (HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK ||
        HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }

    /* STM32 stores a 00-99 year and no timezone. Treat the persisted calendar
     * as UTC in the range 2000-2099. */
    const int32_t year{ 2000 + date.Year };
    if (date.Month == 0U || date.Month > 12U || date.Date == 0U ||
        date.Date > days_in_month(year, date.Month) || time.Hours > 23U || time.Minutes > 59U ||
        time.Seconds > 59U || time.SubSeconds > time.SecondFraction) {
        return HAL_ERROR;
    }

    const int64_t days{ days_from_civil(year, date.Month, date.Date) };
    *seconds_since_epoch = days * 86400 + static_cast<int64_t>(time.Hours) * 3600 +
                           static_cast<int64_t>(time.Minutes) * 60 + time.Seconds;
    *nanoseconds = static_cast<uint32_t>(
      static_cast<uint64_t>(time.SecondFraction - time.SubSeconds) * NANOSECONDS_PER_SECOND /
      (static_cast<uint64_t>(time.SecondFraction) + 1U));
    return HAL_OK;
}

// extern "C" {

// /* These are the lock sentinels exported by Newlib's default lock object.
//  * Providing them here keeps that object (and its no-op lock functions) out of
//  * the link while preserving the ABI expected by the rest of Newlib. */
// __lock __lock___arc4random_mutex{};
// __lock __lock___at_quick_exit_mutex{};
// __lock __lock___atexit_recursive_mutex{};
// __lock __lock___dd_hash_mutex{};
// __lock __lock___env_recursive_mutex{};
// __lock __lock___malloc_recursive_mutex{};
// __lock __lock___sfp_recursive_mutex{};
// __lock __lock___tz_mutex{};

// HAL_StatusTypeDef platform_init_libc_locks(void)
// {
//     if (libc_mutex_ready) {
//         return HAL_OK;
//     }

//     if (tx_mutex_create(&libc_mutex, libc_mutex_name, TX_INHERIT) != TX_SUCCESS) {
//         return HAL_ERROR;
//     }

//     if (tx_mutex_create(&cxx_guard_mutex, cxx_guard_mutex_name, TX_INHERIT) != TX_SUCCESS) {
//         return HAL_ERROR;
//     }

//     libc_mutex_ready = true;
//     cxx_guard_mutex_ready = true;
//     return HAL_OK;
// }

// /* Arm GNU libstdc++ is configured with the "single" thread model, so its
//  * stock guard functions do not wait for another ThreadX thread to finish a
//  * function-local static initializer. Hold one recursive mutex across the
//  * initializer and use each ABI guard's second byte to detect true recursion.
//  * Nested initialization of a different static remains valid because ThreadX
//  * mutexes are recursive. */
// int __cxa_guard_acquire(CxxGuard* guard)
// {
//     if (guard == nullptr) {
//         libc_lock_failure();
//     }

//     if ((load_cxx_guard(guard) & CXX_GUARD_INITIALIZED) != 0U) {
//         return 0;
//     }

//     const bool thread_context = is_cxx_guard_thread_context();
//     if (thread_context && tx_mutex_get(&cxx_guard_mutex, TX_WAIT_FOREVER) != TX_SUCCESS) {
//         libc_lock_failure();
//     }

//     const CxxGuard state = load_cxx_guard(guard);
//     if ((state & CXX_GUARD_INITIALIZED) != 0U) {
//         if (thread_context && tx_mutex_put(&cxx_guard_mutex) != TX_SUCCESS) {
//             libc_lock_failure();
//         }
//         return 0;
//     }

//     if ((state & CXX_GUARD_IN_PROGRESS) != 0U) {
//         libc_lock_failure();
//     }

//     store_cxx_guard(guard, state | CXX_GUARD_IN_PROGRESS);
//     return 1;
// }

// void __cxa_guard_release(CxxGuard* guard)
// {
//     if (guard == nullptr) {
//         libc_lock_failure();
//     }

//     store_cxx_guard(guard, CXX_GUARD_INITIALIZED);

//     if (is_cxx_guard_thread_context() && tx_mutex_put(&cxx_guard_mutex) != TX_SUCCESS) {
//         libc_lock_failure();
//     }
// }

// void __cxa_guard_abort(CxxGuard* guard)
// {
//     if (guard == nullptr) {
//         libc_lock_failure();
//     }

//     store_cxx_guard(guard, 0U);

//     if (is_cxx_guard_thread_context() && tx_mutex_put(&cxx_guard_mutex) != TX_SUCCESS) {
//         libc_lock_failure();
//     }
// }

// void __retarget_lock_init(_LOCK_T* lock)
// {
//     if (lock != nullptr) {
//         *lock = &dynamic_lock_sentinel;
//     }
// }

// void __retarget_lock_init_recursive(_LOCK_T* lock) { __retarget_lock_init(lock); }

// void __retarget_lock_close(_LOCK_T lock) { static_cast<void>(lock); }

// void __retarget_lock_close_recursive(_LOCK_T lock) { __retarget_lock_close(lock); }

// void __retarget_lock_acquire(_LOCK_T lock)
// {
//     static_cast<void>(lock);
//     acquire_libc_lock(TX_WAIT_FOREVER);
// }

// void __retarget_lock_acquire_recursive(_LOCK_T lock) { __retarget_lock_acquire(lock); }

// int __retarget_lock_try_acquire(_LOCK_T lock)
// {
//     static_cast<void>(lock);
//     return try_acquire_libc_lock();
// }

// int __retarget_lock_try_acquire_recursive(_LOCK_T lock) { return __retarget_lock_try_acquire(lock); }

// void __retarget_lock_release(_LOCK_T lock)
// {
//     static_cast<void>(lock);
//     release_libc_lock();
// }

// void __retarget_lock_release_recursive(_LOCK_T lock) { __retarget_lock_release(lock); }
// }
