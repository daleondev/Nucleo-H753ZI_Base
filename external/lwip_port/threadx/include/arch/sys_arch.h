/*
 * lwIP `sys_arch` types implemented on top of Eclipse ThreadX primitives.
 *
 * The same implementation is used for the STM32 firmware and the Linux
 * simulation - in both builds lwIP runs above ThreadX, only the underlying
 * netif differs.
 */

#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

#include "tx_api.h"

#define SYS_MBOX_NULL ((sys_mbox_t){ 0 })
#define SYS_SEM_NULL ((sys_sem_t){ 0 })

#define LWIP_COMPAT_MUTEX_ALLOWED 0

typedef struct
{
    TX_SEMAPHORE sem;
    UINT valid;
} sys_sem_t;

typedef struct
{
    TX_MUTEX mutex;
    UINT valid;
} sys_mutex_t;

typedef struct
{
    TX_QUEUE queue;
    UINT valid;
    void* storage;
    ULONG capacity;
} sys_mbox_t;

typedef TX_THREAD* sys_thread_t;

#endif /* LWIP_ARCH_SYS_ARCH_H */
