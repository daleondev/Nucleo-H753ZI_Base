#ifndef TX_LINUX_STACK_TRACKING_H
#define TX_LINUX_STACK_TRACKING_H

#include <stddef.h>

#include "tx_api.h"

#ifdef __cplusplus
extern "C" {
#endif

VOID _tx_linux_thread_stack_register(TX_THREAD* thread_ptr);
VOID _tx_linux_thread_stack_unregister(VOID);
VOID _tx_linux_thread_stack_capture_current(TX_THREAD* thread_ptr);
VOID _tx_linux_thread_stack_capture_signal_context(VOID* context);
UINT _tx_linux_thread_stack_prepare_host(TX_THREAD* thread_ptr);
VOID _tx_linux_thread_stack_enable_signal_altstack(TX_THREAD* thread_ptr);
VOID _tx_linux_thread_stack_calibrate(TX_THREAD* thread_ptr);
VOID* _tx_linux_thread_stack_host_base(TX_THREAD* thread_ptr);
size_t _tx_linux_thread_stack_host_size(TX_THREAD* thread_ptr);
VOID _tx_linux_thread_stack_refresh(TX_THREAD* thread_ptr);
VOID _tx_linux_thread_stack_refresh_all(VOID);

#ifdef __cplusplus
}
#endif

#endif