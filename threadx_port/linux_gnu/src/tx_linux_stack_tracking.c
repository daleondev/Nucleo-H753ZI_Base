/***************************************************************************
 * Copyright (c) 2024 Microsoft Corporation
 * Copyright (c) 2026-present Eclipse ThreadX contributors
 *
 * This program and the accompanying materials are made available under the
 * terms of the MIT License which is available at
 * https://opensource.org/licenses/MIT.
 *
 * SPDX-License-Identifier: MIT
 **************************************************************************/

#define TX_SOURCE_CODE

#include "tx_api.h"
#include "tx_thread.h"

#include "tx_linux_stack_tracking.h"

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#if !defined(__x86_64__)
#error "The custom Linux ThreadX stack tracking implementation currently supports x86_64 only."
#endif

#define TX_LINUX_STACK_SNAPSHOT_SIGNAL SIGRTMIN

typedef struct TX_LINUX_PTHREAD_STACK_INFO_STRUCT
{
    VOID* host_stack_base;
    size_t host_stack_size;
    stack_t signal_stack;
} TX_LINUX_PTHREAD_STACK_INFO;

static __thread TX_THREAD* _tx_linux_stack_tracking_thread_ptr = TX_NULL;
static sem_t _tx_linux_thread_stack_snapshot_semaphore;
static pthread_mutex_t _tx_linux_thread_stack_snapshot_mutex = PTHREAD_MUTEX_INITIALIZER;

void _tx_linux_thread_suspend(pthread_t thread_id);
void _tx_linux_thread_resume(pthread_t thread_id);

VOID _tx_thread_stack_analyze(TX_THREAD* thread_ptr);

static ULONG _tx_linux_thread_stack_round_up(ULONG value, ULONG alignment)
{
    return ((value + alignment - 1U) / alignment) * alignment;
}

static TX_LINUX_PTHREAD_STACK_INFO* _tx_linux_thread_stack_info(TX_THREAD* thread_ptr)
{
    return (TX_LINUX_PTHREAD_STACK_INFO*)thread_ptr->tx_thread_extension_ptr;
}

static VOID _tx_linux_thread_stack_release_host(TX_THREAD* thread_ptr)
{
    TX_LINUX_PTHREAD_STACK_INFO* stack_info;

    stack_info = _tx_linux_thread_stack_info(thread_ptr);
    if (stack_info == TX_NULL) {
        return;
    }

    free(stack_info->host_stack_base);
    free(stack_info->signal_stack.ss_sp);
    free(stack_info);
    thread_ptr->tx_thread_extension_ptr = TX_NULL;
}

static VOID _tx_linux_thread_stack_update(TX_THREAD* thread_ptr, VOID* stack_ptr)
{
    TX_LINUX_PTHREAD_STACK_INFO* stack_info;
    UCHAR* stack_start;
    UCHAR* stack_end;
    UCHAR* current_stack_ptr;

    if ((thread_ptr == TX_NULL) || (thread_ptr->tx_thread_id != TX_THREAD_ID) || (stack_ptr == TX_NULL)) {
        return;
    }

    if ((thread_ptr->tx_thread_stack_start == TX_NULL) || (thread_ptr->tx_thread_stack_end == TX_NULL)) {
        return;
    }

    stack_info = _tx_linux_thread_stack_info(thread_ptr);
    if (stack_info == TX_NULL) {
        return;
    }

    stack_start = (UCHAR*)thread_ptr->tx_thread_stack_start;
    stack_end = (UCHAR*)thread_ptr->tx_thread_stack_end;
    current_stack_ptr = (UCHAR*)stack_ptr;

    if ((current_stack_ptr < ((UCHAR*)stack_info->host_stack_base)) ||
        (current_stack_ptr >= (((UCHAR*)stack_info->host_stack_base) + stack_info->host_stack_size))) {
        return;
    }

    if (current_stack_ptr < stack_start) {
        thread_ptr->tx_thread_stack_ptr = stack_start;
#ifdef TX_ENABLE_STACK_CHECKING
        thread_ptr->tx_thread_stack_highest_ptr = stack_start;
#endif
        return;
    }

    if (current_stack_ptr > stack_end) {
        current_stack_ptr = stack_end;
    }

    thread_ptr->tx_thread_stack_ptr = current_stack_ptr;

#ifdef TX_ENABLE_STACK_CHECKING
    if ((thread_ptr->tx_thread_stack_highest_ptr == TX_NULL) ||
        (current_stack_ptr < ((UCHAR*)thread_ptr->tx_thread_stack_highest_ptr))) {
        thread_ptr->tx_thread_stack_highest_ptr = current_stack_ptr;
    }
#endif
}

static VOID* _tx_linux_thread_stack_current_pointer(VOID)
{
    VOID* stack_ptr;

    __asm__ volatile("movq %%rsp, %0" : "=r"(stack_ptr));
    return (stack_ptr);
}

VOID _tx_linux_thread_stack_register(TX_THREAD* thread_ptr)
{
    _tx_linux_stack_tracking_thread_ptr = thread_ptr;
}

VOID _tx_linux_thread_stack_unregister(VOID) { _tx_linux_stack_tracking_thread_ptr = TX_NULL; }

VOID _tx_linux_thread_stack_capture_current(TX_THREAD* thread_ptr)
{
    _tx_linux_thread_stack_update(thread_ptr, _tx_linux_thread_stack_current_pointer());
}

VOID _tx_linux_thread_stack_capture_signal_context(VOID* context)
{
    ucontext_t* ucontext;
    VOID* stack_ptr;

    if ((_tx_linux_stack_tracking_thread_ptr == TX_NULL) || (context == TX_NULL)) {
        return;
    }

    ucontext = (ucontext_t*)context;
    stack_ptr = (VOID*)(uintptr_t)ucontext->uc_mcontext.gregs[REG_RSP];
    _tx_linux_thread_stack_update(_tx_linux_stack_tracking_thread_ptr, stack_ptr);
}

VOID _tx_linux_thread_stack_capture_snapshot_signal(VOID* context)
{
    _tx_linux_thread_stack_capture_signal_context(context);
    sem_post(&_tx_linux_thread_stack_snapshot_semaphore);
}

VOID _tx_linux_thread_stack_system_initialize(VOID)
{
    sem_init(&_tx_linux_thread_stack_snapshot_semaphore, 0, 0);
}

UINT _tx_linux_thread_stack_prepare_host(TX_THREAD* thread_ptr)
{
    TX_LINUX_PTHREAD_STACK_INFO* stack_info;
    VOID* host_stack_base;
    VOID* signal_stack_base;
    long page_size;
    ULONG requested_size;
    ULONG minimum_host_size;
    ULONG host_stack_size;
    size_t signal_stack_size;
    requested_size = thread_ptr->tx_thread_stack_size;
    if (requested_size == 0U) {
        return (TX_SIZE_ERROR);
    }

    _tx_linux_thread_stack_release_host(thread_ptr);

    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        page_size = 4096;
    }

    minimum_host_size = requested_size + ((ULONG)PTHREAD_STACK_MIN) + ((ULONG)sizeof(ULONG));

    host_stack_size = _tx_linux_thread_stack_round_up(minimum_host_size, (ULONG)page_size);

    stack_info = (TX_LINUX_PTHREAD_STACK_INFO*)calloc(1, sizeof(TX_LINUX_PTHREAD_STACK_INFO));
    if (stack_info == TX_NULL) {
        return (TX_NO_MEMORY);
    }

    host_stack_base = TX_NULL;
    if (posix_memalign(&host_stack_base, (size_t)page_size, (size_t)host_stack_size) != 0) {
        free(stack_info);
        return (TX_NO_MEMORY);
    }

    signal_stack_size = (size_t)SIGSTKSZ;
    signal_stack_base = malloc(signal_stack_size);
    if (signal_stack_base == TX_NULL) {
        free(host_stack_base);
        free(stack_info);
        return (TX_NO_MEMORY);
    }

    memset(host_stack_base, (int)((UCHAR)TX_STACK_FILL), (size_t)host_stack_size);
    memset(signal_stack_base, 0, signal_stack_size);

    stack_info->host_stack_base = host_stack_base;
    stack_info->host_stack_size = (size_t)host_stack_size;
    stack_info->signal_stack.ss_sp = signal_stack_base;
    stack_info->signal_stack.ss_size = signal_stack_size;
    stack_info->signal_stack.ss_flags = 0;
    thread_ptr->tx_thread_extension_ptr = stack_info;

    thread_ptr->tx_thread_stack_start = host_stack_base;
    thread_ptr->tx_thread_stack_size = host_stack_size - sizeof(ULONG);
    thread_ptr->tx_thread_stack_end = ((UCHAR*)host_stack_base) + thread_ptr->tx_thread_stack_size - 1U;

    return (TX_SUCCESS);
}

VOID _tx_linux_thread_stack_enable_signal_altstack(TX_THREAD* thread_ptr)
{
    TX_LINUX_PTHREAD_STACK_INFO* stack_info;

    stack_info = _tx_linux_thread_stack_info(thread_ptr);
    if (stack_info == TX_NULL) {
        return;
    }

    sigaltstack(&stack_info->signal_stack, TX_NULL);
}

VOID _tx_linux_thread_stack_calibrate(TX_THREAD* thread_ptr)
{
    _tx_linux_thread_stack_capture_current(thread_ptr);
}

VOID* _tx_linux_thread_stack_host_base(TX_THREAD* thread_ptr)
{
    TX_LINUX_PTHREAD_STACK_INFO* stack_info;

    stack_info = _tx_linux_thread_stack_info(thread_ptr);
    if (stack_info == TX_NULL) {
        return (TX_NULL);
    }

    return (stack_info->host_stack_base);
}

size_t _tx_linux_thread_stack_host_size(TX_THREAD* thread_ptr)
{
    TX_LINUX_PTHREAD_STACK_INFO* stack_info;

    stack_info = _tx_linux_thread_stack_info(thread_ptr);
    if (stack_info == TX_NULL) {
        return ((size_t)0);
    }

    return (stack_info->host_stack_size);
}

VOID _tx_linux_thread_stack_refresh(TX_THREAD* thread_ptr)
{
    if ((thread_ptr == TX_NULL) || (thread_ptr->tx_thread_id != TX_THREAD_ID)) {
        return;
    }

    if (pthread_equal(thread_ptr->tx_thread_linux_thread_id, pthread_self())) {
        _tx_linux_thread_stack_capture_current(thread_ptr);
    }

#ifdef TX_ENABLE_STACK_CHECKING
    _tx_thread_stack_analyze(thread_ptr);
#endif
}

VOID _tx_linux_thread_stack_refresh_all(VOID)
{
    TX_THREAD* first_thread;
    TX_THREAD* current_thread;

    first_thread = _tx_thread_created_ptr;
    if ((first_thread == TX_NULL) || (_tx_thread_created_count == 0U)) {
        return;
    }

    current_thread = first_thread;
    do {
        _tx_linux_thread_stack_refresh(current_thread);
        current_thread = current_thread->tx_thread_created_next;
    } while (current_thread != first_thread);
}