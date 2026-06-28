#include "libc.h"

#include <sys/lock.h>

#include <tx_api.h>

#include <stdbool.h>
#include <stdint.h>

/* Newlib deliberately keeps struct __lock opaque and defines its built-in
 * lock objects as one-byte sentinels. All lock identities therefore share a
 * separate ThreadX mutex instead of storing RTOS state inside Newlib memory. */
struct __lock
{
    uint8_t unused;
};
typedef struct __lock __lock_t;

static TX_MUTEX libc_mutex;
static CHAR libc_mutex_name[] = "Newlib libc";
static bool libc_mutex_ready;
static __lock_t dynamic_lock_sentinel;

static void libc_lock_failure(void) { exit(EXIT_FAILURE); }

static bool is_thread_context()
{
    // if (__get_IPSR() != 0U) {
    //     /* Newlib calls may block and are forbidden from interrupt context. */
    //     libc_lock_failure();
    // }

    const TX_THREAD* current_thread = tx_thread_identify();

    if (current_thread == TX_NULL) {
        /* Before the scheduler starts, execution is single-threaded. */
        return false;
    }

    if (!libc_mutex_ready) {
        libc_lock_failure();
    }

    return true;
}

static void acquire_libc_lock(ULONG wait_option)
{
    if (is_thread_context() && tx_mutex_get(&libc_mutex, wait_option) != TX_SUCCESS) {
        libc_lock_failure();
    }
}

static int try_acquire_libc_lock()
{
    if (!is_thread_context()) {
        return 1;
    }

    return tx_mutex_get(&libc_mutex, TX_NO_WAIT) == TX_SUCCESS ? 1 : 0;
}

static void release_libc_lock()
{
    if (is_thread_context() && tx_mutex_put(&libc_mutex) != TX_SUCCESS) {
        libc_lock_failure();
    }
}

/* These are the lock sentinels exported by Newlib's default lock object.
 * Providing them here keeps that object (and its no-op lock functions) out of
 * the link while preserving the ABI expected by the rest of Newlib. */
__lock_t __lock___arc4random_mutex;
__lock_t __lock___at_quick_exit_mutex;
__lock_t __lock___atexit_recursive_mutex;
__lock_t __lock___dd_hash_mutex;
__lock_t __lock___env_recursive_mutex;
__lock_t __lock___malloc_recursive_mutex;
__lock_t __lock___sfp_recursive_mutex;
__lock_t __lock___tz_mutex;

int osal_init_libc(void)
{
    memset(&__lock___arc4random_mutex, 0, sizeof(__lock_t));
    memset(&__lock___at_quick_exit_mutex, 0, sizeof(__lock_t));
    memset(&__lock___atexit_recursive_mutex, 0, sizeof(__lock_t));
    memset(&__lock___dd_hash_mutex, 0, sizeof(__lock_t));
    memset(&__lock___env_recursive_mutex, 0, sizeof(__lock_t));
    memset(&__lock___malloc_recursive_mutex, 0, sizeof(__lock_t));
    memset(&__lock___sfp_recursive_mutex, 0, sizeof(__lock_t));
    memset(&__lock___tz_mutex, 0, sizeof(__lock_t));

    if (libc_mutex_ready) {
        return 0;
    }

    if (tx_mutex_create(&libc_mutex, libc_mutex_name, TX_INHERIT) != TX_SUCCESS) {
        return 1;
    }

    libc_mutex_ready = true;
    return 0;
}

void __retarget_lock_init(_LOCK_T* lock)
{
    if (lock != NULL) {
        *lock = &dynamic_lock_sentinel;
    }
}

void __retarget_lock_init_recursive(_LOCK_T* lock) { __retarget_lock_init(lock); }

void __retarget_lock_close(_LOCK_T lock) { (void)lock; }

void __retarget_lock_close_recursive(_LOCK_T lock) { __retarget_lock_close(lock); }

void __retarget_lock_acquire(_LOCK_T lock)
{
    (void)lock;
    acquire_libc_lock(TX_WAIT_FOREVER);
}

void __retarget_lock_acquire_recursive(_LOCK_T lock) { __retarget_lock_acquire(lock); }

int __retarget_lock_try_acquire(_LOCK_T lock)
{
    (void)lock;
    return try_acquire_libc_lock();
}

int __retarget_lock_try_acquire_recursive(_LOCK_T lock) { return __retarget_lock_try_acquire(lock); }

void __retarget_lock_release(_LOCK_T lock)
{
    (void)lock;
    release_libc_lock();
}

void __retarget_lock_release_recursive(_LOCK_T lock) { __retarget_lock_release(lock); }
