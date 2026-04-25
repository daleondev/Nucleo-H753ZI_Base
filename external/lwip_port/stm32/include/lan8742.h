/*
 * Minimal LAN8742 PHY driver for STM32H7 ETH.
 *
 * Talks to the PHY over MDIO using `HAL_ETH_ReadPHYRegister` /
 * `HAL_ETH_WritePHYRegister`. Only the bits we need to detect link up/down
 * and run auto-negotiation are exposed.
 */

#ifndef LAN8742_H
#define LAN8742_H

#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LAN8742_OK 0
#define LAN8742_ERROR -1

#define LAN8742_LINK_DOWN 0
#define LAN8742_LINK_UP_100MBIT_FD 1
#define LAN8742_LINK_UP_100MBIT_HD 2
#define LAN8742_LINK_UP_10MBIT_FD 3
#define LAN8742_LINK_UP_10MBIT_HD 4

int lan8742Init(ETH_HandleTypeDef* heth, uint32_t phyAddress);
int lan8742GetLinkState(ETH_HandleTypeDef* heth, uint32_t phyAddress);

#ifdef __cplusplus
}
#endif

#endif /* LAN8742_H */
