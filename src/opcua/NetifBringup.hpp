#ifndef OPCUA_NETIF_BRINGUP_HPP
#define OPCUA_NETIF_BRINGUP_HPP

#include <cstdint>

namespace opcua
{
    /*
     * Bring up the platform-specific lwIP netif. Returns true on success.
     * Implemented per target in NetifBringup_{linux,stm32}.cpp.
     */
    bool bringUpNetif();

    constexpr std::uint16_t OPCUA_DEFAULT_PORT{ 4840 };
} // namespace opcua

#endif // OPCUA_NETIF_BRINGUP_HPP
