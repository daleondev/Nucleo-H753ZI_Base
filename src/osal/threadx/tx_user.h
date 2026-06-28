#ifndef OSAL_THREADX_TX_USER_H
#define OSAL_THREADX_TX_USER_H

/* Project-owned ThreadX configuration. Keep generated CubeMX headers free of
 * OSAL policy so CubeMX regeneration cannot overwrite this integration. */

#define TX_DISABLE_PREEMPTION_THRESHOLD
#define TX_DISABLE_NOTIFY_CALLBACKS
#define TX_TIMER_TICKS_PER_SECOND 1000

#if defined(__ARM_EABI__)
/* Switch Newlib's per-thread reentrancy state on context changes. */
#define TX_ENABLE_EXECUTION_CHANGE_NOTIFY
#endif

#if !defined(__ASSEMBLER__)

#if defined(__ARM_EABI__)
#include <sys/reent.h>
#endif

struct TX_THREAD_STRUCT;

#ifdef __cplusplus
extern "C" {
#endif

extern void osal_libstdcxx_initialize(void);

#if defined(__ARM_EABI__)
extern void osal_libc_thread_create(struct TX_THREAD_STRUCT* thread_ptr);
extern void osal_libc_thread_delete(struct TX_THREAD_STRUCT* thread_ptr);
extern void osal_libc_initialize(void);
#endif

#ifdef __cplusplus
}
#endif

#if defined(__ARM_EABI__)

/* Every ThreadX thread owns the libc state used by errno and stdio. */
#define TX_THREAD_USER_EXTENSION struct _reent tx_thread_libc_reent;
#define TX_THREAD_CREATE_INTERNAL_EXTENSION(thread_ptr) \
    osal_libc_thread_create(thread_ptr);

#define OSAL_THREADX_LIBC_INITIALIZE() osal_libc_initialize()

#else

#define OSAL_THREADX_LIBC_INITIALIZE() ((void)0)

#endif

/* ThreadX invokes this after kernel objects are initialized and before
 * tx_application_define(), which is the first safe point for creating OSAL
 * runtime objects. */
#define TX_INITIALIZE_KERNEL_ENTER_EXTENSION \
    do {                                       \
        OSAL_THREADX_LIBC_INITIALIZE();        \
        osal_libstdcxx_initialize();           \
    } while (0);

#if defined(__ARM_EABI__)
/* Newlib may free lazily allocated buffers while reclaiming a reentrancy
 * object. ThreadX invokes this hook with interrupts disabled, so restore the
 * caller's posture during cleanup and disable interrupts again on return. */
#define TX_THREAD_DELETE_PORT_COMPLETION(thread_ptr) \
    do {                                             \
        TX_RESTORE                                   \
        osal_libc_thread_delete(thread_ptr);         \
        TX_DISABLE                                   \
    } while (0);
#endif

#endif /* !defined(__ASSEMBLER__) */

#endif /* OSAL_THREADX_TX_USER_H */
