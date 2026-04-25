/*
 * STM32H7 lwIP netif glue.
 *
 * RX path: HAL_ETH RX descriptors point at custom `pbuf`s allocated from a
 * dedicated pbuf pool in non-cacheable RAM_D2 (same region as the ETH DMA
 * descriptors). On HAL_ETH_RxCpltCallback we post a semaphore, the
 * `ethernetif_input` thread drains all received frames and hands them to
 * tcpip_input.
 *
 * TX path: pbuf chains are described as ETH_BufferTypeDef chains and handed
 * to HAL_ETH_Transmit_IT. HAL_ETH_TxFreeCallback releases the lwIP pbuf
 * after the DMA is done.
 */

#include "lan8742.h"
#include "lwip_port_stm32.h"

#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/snmp.h"
#include "lwip/sys.h"
#include "lwip/tcpip.h"

#include "tx_api.h"

#include "stm32h7xx_hal.h"

#include <string.h>

#define ETHERNETIF_LINK_TIMER_MS 1000U
#define ETHERNETIF_INPUT_TASK_PRIO 6
#define ETHERNETIF_INPUT_STACK_SZ (4 * 1024)
#define LAN8742_PHY_ADDRESS 0U

#ifndef ETH_RX_BUF_SIZE
#define ETH_RX_BUF_SIZE 1536U
#endif

extern ETH_HandleTypeDef heth;
extern ETH_TxPacketConfig TxConfig;

static TX_SEMAPHORE s_rxSem;
static TX_THREAD s_rxThread;
__attribute__((aligned(8))) static uint8_t s_rxThreadStack[ETHERNETIF_INPUT_STACK_SZ];
static struct netif* s_netif;
static volatile uint8_t s_linkUp;

/* ------------------------------------------------------------------------- */

static void ethernetifLowLevelInit(struct netif* netif)
{
    netif->hwaddr_len = ETH_HWADDR_LEN;
    /* MAC is configured by MX_ETH_Init via heth.Init.MACAddr; mirror it
     * back into the netif so lwIP knows the local hardware address. */
    if ((netif->hwaddr[0] | netif->hwaddr[1] | netif->hwaddr[2] | netif->hwaddr[3] | netif->hwaddr[4] |
         netif->hwaddr[5]) == 0) {
        netif->hwaddr[0] = 0x00;
        netif->hwaddr[1] = 0x80;
        netif->hwaddr[2] = 0xE1;
        netif->hwaddr[3] = 0x00;
        netif->hwaddr[4] = 0x00;
        netif->hwaddr[5] = 0x00;
    }

    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    HAL_ETH_RegisterRxAllocateCallback(&heth, HAL_ETH_RxAllocateCallback);
    HAL_ETH_RegisterRxLinkCallback(&heth, HAL_ETH_RxLinkCallback);
    HAL_ETH_RegisterTxFreeCallback(&heth, HAL_ETH_TxFreeCallback);

    (void)lan8742Init(&heth, LAN8742_PHY_ADDRESS);
    HAL_ETH_Start_IT(&heth);
}

/* Called from HAL_ETH_RxCpltCallback below to wake the input thread. */
static void ethernetifNotifyRx(void) { tx_semaphore_put(&s_rxSem); }

static err_t ethernetifLowLevelOutput(struct netif* netif, struct pbuf* p)
{
    (void)netif;
    ETH_BufferTypeDef txBuffers[ETH_TX_DESC_CNT];
    memset(txBuffers, 0, sizeof(txBuffers));

    uint32_t idx = 0;
    struct pbuf* cursor = p;
    while (cursor != NULL && idx < ETH_TX_DESC_CNT) {
        txBuffers[idx].buffer = cursor->payload;
        txBuffers[idx].len = cursor->len;
        if (idx > 0) {
            txBuffers[idx - 1].next = &txBuffers[idx];
        }
        cursor = cursor->next;
        ++idx;
    }
    if (cursor != NULL) {
        return ERR_IF; /* chain too long */
    }

    /* Pin the pbuf so HAL_ETH_TxFreeCallback can release it after DMA. */
    pbuf_ref(p);

    TxConfig.Length = p->tot_len;
    TxConfig.TxBuffer = txBuffers;
    TxConfig.pData = p;

    /* Ensure all data is flushed before the DMA reads it. */
    SCB_CleanDCache_by_Addr((uint32_t*)((uintptr_t)p->payload & ~0x1FUL),
                            (int32_t)((p->tot_len + 32U + 31U) & ~31U));

    if (HAL_ETH_Transmit_IT(&heth, &TxConfig) != HAL_OK) {
        pbuf_free(p);
        return ERR_IF;
    }
    return ERR_OK;
}

void HAL_ETH_TxFreeCallback(uint32_t* buff)
{
    /* The "buff" argument is what we passed as `TxConfig.pData`. */
    pbuf_free((struct pbuf*)buff);
}

/* RxAllocate / RxLink wire HAL_ETH descriptor buffers into lwIP pbufs. */

void HAL_ETH_RxAllocateCallback(uint8_t** buff)
{
    struct pbuf* p = pbuf_alloc(PBUF_RAW, ETH_RX_BUF_SIZE, PBUF_POOL);
    if (p != NULL) {
        *buff = (uint8_t*)p->payload;
        /* Stash the pbuf pointer just before the payload so RxLink can
         * recover it. */
        ((struct pbuf**)p->payload)[-1] = p;
    }
    else {
        *buff = NULL;
    }
}

void HAL_ETH_RxLinkCallback(void** pStart, void** pEnd, uint8_t* buff, uint16_t length)
{
    struct pbuf** start = (struct pbuf**)pStart;
    struct pbuf** end = (struct pbuf**)pEnd;
    struct pbuf* p = ((struct pbuf**)buff)[-1];

    p->next = NULL;
    p->len = length;
    p->tot_len = length;

    if (*start == NULL) {
        *start = p;
    }
    else {
        (*end)->next = p;
        (*end)->tot_len = (uint16_t)((*end)->tot_len + length);
    }
    *end = p;
}

void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef* eth)
{
    (void)eth;
    ethernetifNotifyRx();
}

static void ethernetifInputTask(ULONG arg)
{
    (void)arg;
    while (1) {
        if (tx_semaphore_get(&s_rxSem, TX_WAIT_FOREVER) != TX_SUCCESS) {
            continue;
        }
        struct pbuf* p = NULL;
        while (HAL_ETH_ReadData(&heth, (void**)&p) == HAL_OK && p != NULL) {
            SCB_InvalidateDCache_by_Addr((uint32_t*)((uintptr_t)p->payload & ~0x1FUL),
                                         (int32_t)((p->tot_len + 32U + 31U) & ~31U));
            if (s_netif->input(p, s_netif) != ERR_OK) {
                pbuf_free(p);
            }
            p = NULL;
        }
    }
}

static void ethernetifLinkPoll(void* arg)
{
    struct netif* netif = (struct netif*)arg;
    int state = lan8742GetLinkState(&heth, LAN8742_PHY_ADDRESS);

    if (state == LAN8742_LINK_DOWN) {
        if (s_linkUp) {
            netif_set_link_down(netif);
            s_linkUp = 0;
        }
    }
    else if (state >= LAN8742_LINK_UP_100MBIT_FD) {
        if (!s_linkUp) {
            netif_set_link_up(netif);
            s_linkUp = 1;
        }
    }
    sys_timeout(ETHERNETIF_LINK_TIMER_MS, ethernetifLinkPoll, netif);
}

static err_t ethernetifInit(struct netif* netif)
{
    netif->name[0] = 's';
    netif->name[1] = 't';
    netif->output = etharp_output;
    netif->linkoutput = ethernetifLowLevelOutput;
    ethernetifLowLevelInit(netif);
    return ERR_OK;
}

static volatile int s_tcpipReady;
static void ethernetifTcpipInitDone(void* arg)
{
    (void)arg;
    s_tcpipReady = 1;
}

err_t lwipPortStm32Start(struct netif* netif, u32_t ipAddrHbo, u32_t netmaskHbo, u32_t gatewayHbo)
{
    if (netif == NULL) {
        return ERR_ARG;
    }
    s_netif = netif;

    if (tx_semaphore_create(&s_rxSem, (CHAR*)"eth_rx", 0) != TX_SUCCESS) {
        return ERR_MEM;
    }
    if (tx_thread_create(&s_rxThread,
                         (CHAR*)"eth_in",
                         ethernetifInputTask,
                         0,
                         s_rxThreadStack,
                         sizeof(s_rxThreadStack),
                         ETHERNETIF_INPUT_TASK_PRIO,
                         ETHERNETIF_INPUT_TASK_PRIO,
                         TX_NO_TIME_SLICE,
                         TX_AUTO_START) != TX_SUCCESS) {
        return ERR_MEM;
    }

    tcpip_init(ethernetifTcpipInitDone, NULL);
    while (!s_tcpipReady) {
        tx_thread_sleep(1);
    }

    ip4_addr_t ipaddr;
    ip4_addr_t netmask;
    ip4_addr_t gw;
    ipaddr.addr = lwip_htonl(ipAddrHbo);
    netmask.addr = lwip_htonl(netmaskHbo);
    gw.addr = lwip_htonl(gatewayHbo);

    if (netif_add(netif, &ipaddr, &netmask, &gw, NULL, ethernetifInit, tcpip_input) == NULL) {
        return ERR_IF;
    }
    netif_set_default(netif);
    netif_set_up(netif);

    sys_timeout(ETHERNETIF_LINK_TIMER_MS, ethernetifLinkPoll, netif);
    return ERR_OK;
}
