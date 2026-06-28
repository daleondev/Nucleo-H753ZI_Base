#ifndef RUNTIME_THREADX_TX_USER_H
#define RUNTIME_THREADX_TX_USER_H

/* Project-owned ThreadX configuration. Keep generated CubeMX headers free of
 * runtime policy so CubeMX regeneration cannot overwrite this integration. */

#define TX_DISABLE_PREEMPTION_THRESHOLD
#define TX_TIMER_TICKS_PER_SECOND 1000
#define RUNTIME_THREAD_KEY_COUNT 16

/* The C++ runtime reserves each thread's entry/exit notification callback to
 * run gthread key destructors required by futures and at-thread-exit APIs;
 * application code must not replace that callback. */

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

extern void runtime_libstdcxx_initialize(void);
extern void runtime_libstdcxx_thread_create(struct TX_THREAD_STRUCT* thread_ptr);
extern void runtime_libstdcxx_thread_started(struct TX_THREAD_STRUCT* thread_ptr);

#if defined(__ARM_EABI__)
extern void runtime_libc_thread_create(struct TX_THREAD_STRUCT* thread_ptr);
extern void runtime_libc_thread_delete(struct TX_THREAD_STRUCT* thread_ptr);
extern void runtime_libc_initialize(void);
#endif

#ifdef __cplusplus
}
#endif

#if defined(__ARM_EABI__)

/* Every ThreadX thread owns the libc state used by errno and stdio. */
#define RUNTIME_THREADX_LIBC_USER_EXTENSION struct _reent tx_thread_libc_reent;
#define RUNTIME_THREADX_LIBC_CREATE(thread_ptr) runtime_libc_thread_create(thread_ptr);

#define RUNTIME_THREADX_LIBC_INITIALIZE() runtime_libc_initialize()

#else

#define RUNTIME_THREADX_LIBC_USER_EXTENSION
#define RUNTIME_THREADX_LIBC_CREATE(thread_ptr)
#define RUNTIME_THREADX_LIBC_INITIALIZE() ((void)0)

#endif

#define TX_THREAD_USER_EXTENSION                                      \
    RUNTIME_THREADX_LIBC_USER_EXTENSION                               \
    void* tx_thread_runtime_tls_values[RUNTIME_THREAD_KEY_COUNT];     \
    unsigned int tx_thread_runtime_tls_generations[RUNTIME_THREAD_KEY_COUNT];

#define TX_THREAD_CREATE_INTERNAL_EXTENSION(thread_ptr) \
    do {                                                \
        RUNTIME_THREADX_LIBC_CREATE(thread_ptr)         \
        runtime_libstdcxx_thread_create(thread_ptr);    \
    } while (0);

#define TX_THREAD_STARTED_EXTENSION(thread_ptr) \
    runtime_libstdcxx_thread_started(thread_ptr);

/* ThreadX invokes this after kernel objects are initialized and before
 * tx_application_define(), which is the first safe point for creating
 * runtime objects. */
#define TX_INITIALIZE_KERNEL_ENTER_EXTENSION \
    do {                                       \
        RUNTIME_THREADX_LIBC_INITIALIZE();     \
        runtime_libstdcxx_initialize();        \
    } while (0);

#if defined(__ARM_EABI__)
/* Newlib may free lazily allocated buffers while reclaiming a reentrancy
 * object. ThreadX invokes this hook with interrupts disabled, so restore the
 * caller's posture during cleanup and disable interrupts again on return. */
#define TX_THREAD_DELETE_PORT_COMPLETION(thread_ptr) \
    do {                                             \
        TX_RESTORE                                   \
        runtime_libc_thread_delete(thread_ptr);      \
        TX_DISABLE                                   \
    } while (0);
#endif

#endif /* !defined(__ASSEMBLER__) */

#endif /* RUNTIME_THREADX_TX_USER_H */
