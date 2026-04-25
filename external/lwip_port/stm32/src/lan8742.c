/* Minimal LAN8742 PHY driver - see lan8742.h for the contract. */

#include "lan8742.h"

#define PHY_BCR 0x0000U
#define PHY_BSR 0x0001U
#define PHY_SR 0x001FU

#define PHY_BCR_SOFT_RESET 0x8000U
#define PHY_BCR_AUTONEG_EN 0x1000U
#define PHY_BCR_AUTONEG_RESTART 0x0200U

#define PHY_BSR_LINK_STATUS 0x0004U
#define PHY_BSR_AUTONEG_DONE 0x0020U

#define PHY_SR_AUTODONE 0x1000U
#define PHY_SR_SPEED_MASK 0x001CU
#define PHY_SR_SPEED_10HD 0x0004U
#define PHY_SR_SPEED_100HD 0x0008U
#define PHY_SR_SPEED_10FD 0x0014U
#define PHY_SR_SPEED_100FD 0x0018U

int lan8742Init(ETH_HandleTypeDef* heth, uint32_t phyAddress)
{
    if (heth == NULL) {
        return LAN8742_ERROR;
    }
    if (HAL_ETH_WritePHYRegister(heth, phyAddress, PHY_BCR, PHY_BCR_SOFT_RESET) != HAL_OK) {
        return LAN8742_ERROR;
    }
    /* Wait for the reset bit to clear (bounded busy-wait). */
    uint32_t value = 0;
    for (uint32_t i = 0; i < 0x000FFFFFUL; ++i) {
        if (HAL_ETH_ReadPHYRegister(heth, phyAddress, PHY_BCR, &value) != HAL_OK) {
            return LAN8742_ERROR;
        }
        if ((value & PHY_BCR_SOFT_RESET) == 0U) {
            break;
        }
    }
    if ((value & PHY_BCR_SOFT_RESET) != 0U) {
        return LAN8742_ERROR;
    }
    if (HAL_ETH_WritePHYRegister(heth, phyAddress, PHY_BCR, PHY_BCR_AUTONEG_EN | PHY_BCR_AUTONEG_RESTART) !=
        HAL_OK) {
        return LAN8742_ERROR;
    }
    return LAN8742_OK;
}

int lan8742GetLinkState(ETH_HandleTypeDef* heth, uint32_t phyAddress)
{
    uint32_t bsr = 0;
    if (HAL_ETH_ReadPHYRegister(heth, phyAddress, PHY_BSR, &bsr) != HAL_OK) {
        return LAN8742_ERROR;
    }
    if ((bsr & PHY_BSR_LINK_STATUS) == 0U) {
        return LAN8742_LINK_DOWN;
    }
    uint32_t sr = 0;
    if (HAL_ETH_ReadPHYRegister(heth, phyAddress, PHY_SR, &sr) != HAL_OK) {
        return LAN8742_ERROR;
    }
    switch (sr & PHY_SR_SPEED_MASK) {
        case PHY_SR_SPEED_100FD:
            return LAN8742_LINK_UP_100MBIT_FD;
        case PHY_SR_SPEED_100HD:
            return LAN8742_LINK_UP_100MBIT_HD;
        case PHY_SR_SPEED_10FD:
            return LAN8742_LINK_UP_10MBIT_FD;
        case PHY_SR_SPEED_10HD:
            return LAN8742_LINK_UP_10MBIT_HD;
        default:
            return LAN8742_LINK_DOWN;
    }
}
