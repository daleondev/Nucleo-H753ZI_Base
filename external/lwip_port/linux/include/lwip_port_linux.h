/*
 * Linux simulation lwIP netif glue: brings up an lwIP netif on a TUN/TAP
 * device created and configured outside the application (see
 * `scripts/setup_tap.sh`).
 */

#ifndef LWIP_PORT_LINUX_H
#define LWIP_PORT_LINUX_H

#include "lwip/netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialise lwIP (`tcpip_init`) and attach a TAP-backed netif.
 * `tapDevice` may be NULL (defaults to "tap0").
 * Static addressing is used so the OPC UA server is reachable at a known IP.
 */
err_t lwipPortLinuxStart(struct netif* netif,
                         const char* tapDevice,
                         u32_t ipAddrHbo,
                         u32_t netmaskHbo,
                         u32_t gatewayHbo);

#ifdef __cplusplus
}
#endif

#endif /* LWIP_PORT_LINUX_H */
