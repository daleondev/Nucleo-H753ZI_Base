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

    /*
     * Returns the host string (dotted-IP or DNS hostname) the OPC UA server
     * should advertise in discovery URLs and the listen socket label. Valid
     * only after a successful bringUpNetif() call. Never null.
     */
    const char* getServerHost();

    constexpr std::uint16_t OPCUA_DEFAULT_PORT{ 4840 };
} // namespace opcua

#endif // OPCUA_NETIF_BRINGUP_HPP
