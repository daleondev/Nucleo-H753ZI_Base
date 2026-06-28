#ifndef OSAL_THREADX_TX_USER_H
#define OSAL_THREADX_TX_USER_H

/* Project-owned ThreadX configuration. Keep generated CubeMX headers free of
 * OSAL policy so CubeMX regeneration cannot overwrite this integration. */

#define TX_DISABLE_PREEMPTION_THRESHOLD
#define TX_DISABLE_NOTIFY_CALLBACKS
#define TX_TIMER_TICKS_PER_SECOND 1000

/* Required to switch Newlib's per-thread reentrancy state on context changes. */
#define TX_ENABLE_EXECUTION_CHANGE_NOTIFY

#if !defined(__ASSEMBLER__)

#include <sys/reent.h>

struct TX_THREAD_STRUCT;

#ifdef __cplusplus
extern "C" {
#endif

extern void osal_libc_thread_create(struct TX_THREAD_STRUCT* thread_ptr);
extern void osal_libc_thread_delete(struct TX_THREAD_STRUCT* thread_ptr);
extern void osal_libc_initialize(void);

#ifdef __cplusplus
}
#endif

/* Every ThreadX thread owns the libc state used by errno and stdio. */
#define TX_THREAD_USER_EXTENSION struct _reent tx_thread_libc_reent;
#define TX_THREAD_CREATE_INTERNAL_EXTENSION(thread_ptr) \
    osal_libc_thread_create(thread_ptr);

/* ThreadX invokes this after kernel objects are initialized and before
 * tx_application_define(), which is the first safe point for creating the
 * mutexes used by libc. */
#define TX_INITIALIZE_KERNEL_ENTER_EXTENSION osal_libc_initialize();

/* Newlib may free lazily allocated buffers while reclaiming a reentrancy
 * object. ThreadX invokes this hook with interrupts disabled, so restore the
 * caller's posture during cleanup and disable interrupts again on return. */
#define TX_THREAD_DELETE_PORT_COMPLETION(thread_ptr) \
    do {                                             \
        TX_RESTORE                                   \
        osal_libc_thread_delete(thread_ptr);         \
        TX_DISABLE                                   \
    } while (0);

#endif /* !defined(__ASSEMBLER__) */

#endif /* OSAL_THREADX_TX_USER_H */
