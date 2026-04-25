/*
 * lwIP OS abstraction (`sys_arch`) implemented on Eclipse ThreadX primitives.
 *
 * Used on both targets:
 *   - STM32 firmware: bare-metal ThreadX
 *   - Linux simulation: ThreadX-on-Linux/POSIX
 *
 * Stacks for `sys_thread_new` are allocated from a private TX byte pool so
 * lwIP threads do not have to know about static stack arrays.
 */

#include "lwip/debug.h"
#include "lwip/mem.h"
#include "lwip/opt.h"
#include "lwip/stats.h"
#include "lwip/sys.h"

#include "tx_api.h"

#include <stdlib.h>
#include <string.h>

/* ----- Internal state ----------------------------------------------------- */

#define LWIP_PORT_BYTE_POOL_SIZE (96 * 1024)

static UCHAR s_lwipBytePoolMemory[LWIP_PORT_BYTE_POOL_SIZE] __attribute__((aligned(8)));
static TX_BYTE_POOL s_lwipBytePool;
static UINT s_lwipBytePoolReady;

static void* sysArchAllocate(ULONG bytes)
{
    void* ptr = NULL;
    if (tx_byte_allocate(&s_lwipBytePool, &ptr, bytes, TX_NO_WAIT) != TX_SUCCESS) {
        return NULL;
    }
    return ptr;
}

static u32_t sysArchTicksToMs(ULONG ticks) { return (u32_t)((ticks * 1000UL) / TX_TIMER_TICKS_PER_SECOND); }

static ULONG sysArchMsToTicks(u32_t milliseconds)
{
    if (milliseconds == 0) {
        return TX_NO_WAIT;
    }
    ULONG ticks = ((ULONG)milliseconds * TX_TIMER_TICKS_PER_SECOND + 999UL) / 1000UL;
    if (ticks == 0) {
        ticks = 1UL;
    }
    return ticks;
}

/* ----- sys_init / sys_now / sys_jiffies ----------------------------------- */

void sys_init(void)
{
    if (s_lwipBytePoolReady) {
        return;
    }
    if (tx_byte_pool_create(
          &s_lwipBytePool, (CHAR*)"lwip_pool", s_lwipBytePoolMemory, LWIP_PORT_BYTE_POOL_SIZE) !=
        TX_SUCCESS) {
        LWIP_ASSERT("sys_init: byte pool create failed", 0);
        return;
    }
    s_lwipBytePoolReady = 1;
}

u32_t sys_now(void) { return sysArchTicksToMs(tx_time_get()); }

u32_t sys_jiffies(void) { return (u32_t)tx_time_get(); }

/* ----- Semaphores --------------------------------------------------------- */

err_t sys_sem_new(sys_sem_t* sem, u8_t count)
{
    if (sem == NULL) {
        return ERR_ARG;
    }
    if (tx_semaphore_create(&sem->sem, (CHAR*)"lwip_sem", count) != TX_SUCCESS) {
        sem->valid = 0;
        return ERR_MEM;
    }
    sem->valid = 1;
    return ERR_OK;
}

void sys_sem_free(sys_sem_t* sem)
{
    if (sem == NULL || !sem->valid) {
        return;
    }
    tx_semaphore_delete(&sem->sem);
    sem->valid = 0;
}

void sys_sem_signal(sys_sem_t* sem)
{
    if (sem == NULL || !sem->valid) {
        return;
    }
    tx_semaphore_put(&sem->sem);
}

u32_t sys_arch_sem_wait(sys_sem_t* sem, u32_t timeout)
{
    if (sem == NULL || !sem->valid) {
        return SYS_ARCH_TIMEOUT;
    }
    ULONG start = tx_time_get();
    ULONG ticks = (timeout == 0) ? TX_WAIT_FOREVER : sysArchMsToTicks(timeout);
    UINT status = tx_semaphore_get(&sem->sem, ticks);
    if (status != TX_SUCCESS) {
        return SYS_ARCH_TIMEOUT;
    }
    return sysArchTicksToMs(tx_time_get() - start);
}

int sys_sem_valid(sys_sem_t* sem) { return (sem != NULL && sem->valid) ? 1 : 0; }

void sys_sem_set_invalid(sys_sem_t* sem)
{
    if (sem != NULL) {
        sem->valid = 0;
    }
}

/* ----- Mutexes ------------------------------------------------------------ */

err_t sys_mutex_new(sys_mutex_t* mutex)
{
    if (mutex == NULL) {
        return ERR_ARG;
    }
    if (tx_mutex_create(&mutex->mutex, (CHAR*)"lwip_mtx", TX_INHERIT) != TX_SUCCESS) {
        mutex->valid = 0;
        return ERR_MEM;
    }
    mutex->valid = 1;
    return ERR_OK;
}

void sys_mutex_lock(sys_mutex_t* mutex)
{
    if (mutex != NULL && mutex->valid) {
        tx_mutex_get(&mutex->mutex, TX_WAIT_FOREVER);
    }
}

void sys_mutex_unlock(sys_mutex_t* mutex)
{
    if (mutex != NULL && mutex->valid) {
        tx_mutex_put(&mutex->mutex);
    }
}

void sys_mutex_free(sys_mutex_t* mutex)
{
    if (mutex == NULL || !mutex->valid) {
        return;
    }
    tx_mutex_delete(&mutex->mutex);
    mutex->valid = 0;
}

int sys_mutex_valid(sys_mutex_t* mutex) { return (mutex != NULL && mutex->valid) ? 1 : 0; }

void sys_mutex_set_invalid(sys_mutex_t* mutex)
{
    if (mutex != NULL) {
        mutex->valid = 0;
    }
}

/* ----- Mailboxes ---------------------------------------------------------- */

err_t sys_mbox_new(sys_mbox_t* mbox, int size)
{
    if (mbox == NULL || size <= 0) {
        return ERR_ARG;
    }
    /* TX queues take size in 32-bit words; we transport `void*` per slot. */
    ULONG slotWords = sizeof(void*) / sizeof(ULONG);
    if (slotWords == 0) {
        slotWords = 1;
    }
    ULONG storageBytes = (ULONG)size * sizeof(void*);
    void* storage = sysArchAllocate(storageBytes);
    if (storage == NULL) {
        return ERR_MEM;
    }
    if (tx_queue_create(&mbox->queue, (CHAR*)"lwip_mbox", slotWords, storage, storageBytes) != TX_SUCCESS) {
        return ERR_MEM;
    }
    mbox->valid = 1;
    mbox->storage = storage;
    mbox->capacity = (ULONG)size;
    return ERR_OK;
}

void sys_mbox_free(sys_mbox_t* mbox)
{
    if (mbox == NULL || !mbox->valid) {
        return;
    }
    tx_queue_delete(&mbox->queue);
    /* Storage came from the byte pool: leak-on-free is acceptable for this
     * embedded use, but try to release explicitly when the API supports it. */
    if (mbox->storage != NULL) {
        tx_byte_release(mbox->storage);
        mbox->storage = NULL;
    }
    mbox->valid = 0;
}

void sys_mbox_post(sys_mbox_t* mbox, void* msg)
{
    if (mbox == NULL || !mbox->valid) {
        return;
    }
    void* payload = msg;
    while (tx_queue_send(&mbox->queue, &payload, TX_WAIT_FOREVER) != TX_SUCCESS) {
        /* Block until the message is enqueued. */
    }
}

err_t sys_mbox_trypost(sys_mbox_t* mbox, void* msg)
{
    if (mbox == NULL || !mbox->valid) {
        return ERR_ARG;
    }
    void* payload = msg;
    if (tx_queue_send(&mbox->queue, &payload, TX_NO_WAIT) != TX_SUCCESS) {
        return ERR_MEM;
    }
    return ERR_OK;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t* mbox, void* msg) { return sys_mbox_trypost(mbox, msg); }

u32_t sys_arch_mbox_fetch(sys_mbox_t* mbox, void** msg, u32_t timeout)
{
    if (mbox == NULL || !mbox->valid) {
        return SYS_ARCH_TIMEOUT;
    }
    ULONG start = tx_time_get();
    ULONG ticks = (timeout == 0) ? TX_WAIT_FOREVER : sysArchMsToTicks(timeout);
    void* payload = NULL;
    UINT status = tx_queue_receive(&mbox->queue, &payload, ticks);
    if (status != TX_SUCCESS) {
        return SYS_ARCH_TIMEOUT;
    }
    if (msg != NULL) {
        *msg = payload;
    }
    return sysArchTicksToMs(tx_time_get() - start);
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t* mbox, void** msg)
{
    if (mbox == NULL || !mbox->valid) {
        return SYS_MBOX_EMPTY;
    }
    void* payload = NULL;
    if (tx_queue_receive(&mbox->queue, &payload, TX_NO_WAIT) != TX_SUCCESS) {
        return SYS_MBOX_EMPTY;
    }
    if (msg != NULL) {
        *msg = payload;
    }
    return 0;
}

int sys_mbox_valid(sys_mbox_t* mbox) { return (mbox != NULL && mbox->valid) ? 1 : 0; }

void sys_mbox_set_invalid(sys_mbox_t* mbox)
{
    if (mbox != NULL) {
        mbox->valid = 0;
    }
}

/* ----- Threads ------------------------------------------------------------ */

typedef struct
{
    void (*fn)(void*);
    void* arg;
    int inUse;
} sys_thread_trampoline_arg_t;

/* ThreadX `tx_thread_create` only forwards a single ULONG entry argument,
 * which is 32-bit on the x86_64 Linux port. Passing the trampoline pointer
 * directly truncates it; instead we keep a small static table and use the
 * slot index as the entry argument. */
#define SYS_THREAD_TRAMPOLINE_SLOTS 16
static sys_thread_trampoline_arg_t s_threadTrampolines[SYS_THREAD_TRAMPOLINE_SLOTS];
static TX_MUTEX s_threadTrampolineMutex;
static int s_threadTrampolineMutexReady;

static void sysThreadEntry(ULONG argument)
{
    const ULONG slot = argument;
    if (slot >= SYS_THREAD_TRAMPOLINE_SLOTS) {
        return;
    }
    sys_thread_trampoline_arg_t* trampoline = &s_threadTrampolines[slot];
    trampoline->fn(trampoline->arg);
    /* Trampoline slot stays reserved; threadx threads we create here are
     * long-lived (tcpip thread, drivers, etc.) and never destroyed. */
}

sys_thread_t sys_thread_new(const char* name, lwip_thread_fn fn, void* arg, int stacksize, int prio)
{
    if (!s_lwipBytePoolReady) {
        sys_init();
    }
    if (!s_threadTrampolineMutexReady) {
        if (tx_mutex_create(&s_threadTrampolineMutex, (CHAR*)"lwip_thr_mtx", TX_NO_INHERIT) != TX_SUCCESS) {
            return NULL;
        }
        s_threadTrampolineMutexReady = 1;
    }
    TX_THREAD* thread = (TX_THREAD*)sysArchAllocate(sizeof(TX_THREAD));
    if (thread == NULL) {
        return NULL;
    }
    void* stack = sysArchAllocate((ULONG)stacksize);
    if (stack == NULL) {
        return NULL;
    }

    /* Reserve a trampoline slot. */
    ULONG slot = SYS_THREAD_TRAMPOLINE_SLOTS;
    tx_mutex_get(&s_threadTrampolineMutex, TX_WAIT_FOREVER);
    for (ULONG i = 0; i < SYS_THREAD_TRAMPOLINE_SLOTS; ++i) {
        if (!s_threadTrampolines[i].inUse) {
            s_threadTrampolines[i].fn = fn;
            s_threadTrampolines[i].arg = arg;
            s_threadTrampolines[i].inUse = 1;
            slot = i;
            break;
        }
    }
    tx_mutex_put(&s_threadTrampolineMutex);
    if (slot == SYS_THREAD_TRAMPOLINE_SLOTS) {
        return NULL;
    }

    UINT status = tx_thread_create(thread,
                                   (CHAR*)(name != NULL ? name : "lwip"),
                                   sysThreadEntry,
                                   slot,
                                   stack,
                                   (ULONG)stacksize,
                                   (UINT)prio,
                                   (UINT)prio,
                                   TX_NO_TIME_SLICE,
                                   TX_AUTO_START);
    if (status != TX_SUCCESS) {
        s_threadTrampolines[slot].inUse = 0;
        return NULL;
    }
    return thread;
}

/* ----- Critical sections -------------------------------------------------- */

#if SYS_LIGHTWEIGHT_PROT
sys_prot_t sys_arch_protect(void) { return (sys_prot_t)tx_interrupt_control(TX_INT_DISABLE); }

void sys_arch_unprotect(sys_prot_t pval) { (void)tx_interrupt_control((UINT)pval); }
#endif
