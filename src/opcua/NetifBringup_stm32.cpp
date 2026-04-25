#include "NetifBringup.hpp"

#include "lwip/netif.h"
#include "lwip_port_stm32.h"

#include "eth.h"
#include "main.h"

#include <cstdio>

namespace opcua
{
    namespace
    {
        netif g_lwipNetif{};
    }

    bool bringUpNetif()
    {
        MX_ETH_Init();

        const u32_t ipAddr = (10U << 24) | (10U << 16) | (10U << 8) | 2U;
        const u32_t netmask = 0xFFFFFF00U;
        const u32_t gateway = (10U << 24) | (10U << 16) | (10U << 8) | 1U;

        if (lwipPortStm32Start(&g_lwipNetif, ipAddr, netmask, gateway) != ERR_OK) {
            std::printf("netif: eth bring-up failed\n");
            return false;
        }
        return true;
    }
} // namespace opcua
