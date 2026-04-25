#include "NetifBringup.hpp"

#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip_port_linux.h"

#include <cstdio>

namespace opcua
{
    namespace
    {
        netif g_lwipNetif{};
    }

    bool bringUpNetif()
    {
        const u32_t ipAddr = (10U << 24) | (10U << 16) | (10U << 8) | 2U;
        const u32_t netmask = 0xFFFFFF00U;
        const u32_t gateway = (10U << 24) | (10U << 16) | (10U << 8) | 1U;

        if (lwipPortLinuxStart(&g_lwipNetif, "tap0", ipAddr, netmask, gateway) != ERR_OK) {
            std::printf("netif: tap bring-up failed\n");
            std::fflush(stdout);
            return false;
        }
        return true;
    }
} // namespace opcua
