#ifndef LWIP_PORT_STM32_H
#define LWIP_PORT_STM32_H

#include "lwip/netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialise lwIP and bring up the on-board ETH netif (LAN8742 over RMII).
 * MX_ETH_Init() must already have configured the HAL_ETH peripheral.
 */
err_t lwipPortStm32Start(struct netif* netif, u32_t ipAddrHbo, u32_t netmaskHbo, u32_t gatewayHbo);

#ifdef __cplusplus
}
#endif

#endif /* LWIP_PORT_STM32_H */
