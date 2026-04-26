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
        constexpr const char* STM32_HOSTNAME{ "stm32-nucleo" };
        constexpr const char* STM32_IP_STR{ "10.10.10.2" };
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
#if LWIP_NETIF_HOSTNAME
        netif_set_hostname(&g_lwipNetif, STM32_HOSTNAME);
#endif
        std::printf("netif: ip=%s hostname=%s\n", STM32_IP_STR, STM32_HOSTNAME);
        return true;
    }

    // OPC UA serverUrl must resolve at bind time. lwIP has no DNS resolver in
    // this build, so we advertise the dotted IP. The hostname is still set on
    // the netif (DHCP/mDNS) above.
    const char* getServerHost() { return STM32_IP_STR; }
} // namespace opcua
