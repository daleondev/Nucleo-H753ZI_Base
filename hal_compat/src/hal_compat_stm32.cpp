#include "hal_compat/hal_compat_stm32.hpp"

#include <sys/lock.h>

#include <tx_api.h>

#include <cstdint>

/* Newlib deliberately keeps struct __lock opaque and defines its built-in
 * lock objects as one-byte sentinels. All lock identities therefore share a
 * separate ThreadX mutex instead of storing RTOS state inside Newlib memory. */
struct __lock
{
    std::uint8_t unused;
};

namespace
{
    TX_MUTEX libc_mutex;
    CHAR libc_mutex_name[] = "Newlib libc";
    bool libc_mutex_ready{};
    __lock dynamic_lock_sentinel{};

    [[noreturn]] void LibcLockFailure()
    {
        Error_Handler();
        while (true) {
        }
    }

    bool IsThreadContext()
    {
        if (__get_IPSR() != 0U) {
            /* Newlib calls may block and are forbidden from interrupt context. */
            LibcLockFailure();
        }

        const auto* current_thread = tx_thread_identify();

        if (current_thread == TX_NULL) {
            /* Before the scheduler starts, execution is single-threaded. */
            return false;
        }

        if (!libc_mutex_ready) {
            LibcLockFailure();
        }

        return true;
    }

    void AcquireLibcLock(ULONG wait_option)
    {
        if (IsThreadContext() && tx_mutex_get(&libc_mutex, wait_option) != TX_SUCCESS) {
            LibcLockFailure();
        }
    }

    int TryAcquireLibcLock()
    {
        if (!IsThreadContext()) {
            return 1;
        }

        return tx_mutex_get(&libc_mutex, TX_NO_WAIT) == TX_SUCCESS ? 1 : 0;
    }

    void ReleaseLibcLock()
    {
        if (IsThreadContext() && tx_mutex_put(&libc_mutex) != TX_SUCCESS) {
            LibcLockFailure();
        }
    }
}

extern "C" {

/* These are the lock sentinels exported by Newlib's default lock object.
 * Providing them here keeps that object (and its no-op lock functions) out of
 * the link while preserving the ABI expected by the rest of Newlib. */
__lock __lock___arc4random_mutex{};
__lock __lock___at_quick_exit_mutex{};
__lock __lock___atexit_recursive_mutex{};
__lock __lock___dd_hash_mutex{};
__lock __lock___env_recursive_mutex{};
__lock __lock___malloc_recursive_mutex{};
__lock __lock___sfp_recursive_mutex{};
__lock __lock___tz_mutex{};

HAL_StatusTypeDef Platform_InitLibcLocks(void)
{
    if (libc_mutex_ready) {
        return HAL_OK;
    }

    if (tx_mutex_create(&libc_mutex, libc_mutex_name, TX_INHERIT) != TX_SUCCESS) {
        return HAL_ERROR;
    }

    libc_mutex_ready = true;
    return HAL_OK;
}

void __retarget_lock_init(_LOCK_T* lock)
{
    if (lock != nullptr) {
        *lock = &dynamic_lock_sentinel;
    }
}

void __retarget_lock_init_recursive(_LOCK_T* lock) { __retarget_lock_init(lock); }

void __retarget_lock_close(_LOCK_T lock) { static_cast<void>(lock); }

void __retarget_lock_close_recursive(_LOCK_T lock) { __retarget_lock_close(lock); }

void __retarget_lock_acquire(_LOCK_T lock)
{
    static_cast<void>(lock);
    AcquireLibcLock(TX_WAIT_FOREVER);
}

void __retarget_lock_acquire_recursive(_LOCK_T lock) { __retarget_lock_acquire(lock); }

int __retarget_lock_try_acquire(_LOCK_T lock)
{
    static_cast<void>(lock);
    return TryAcquireLibcLock();
}

int __retarget_lock_try_acquire_recursive(_LOCK_T lock) { return __retarget_lock_try_acquire(lock); }

void __retarget_lock_release(_LOCK_T lock)
{
    static_cast<void>(lock);
    ReleaseLibcLock();
}

void __retarget_lock_release_recursive(_LOCK_T lock) { __retarget_lock_release(lock); }

}
