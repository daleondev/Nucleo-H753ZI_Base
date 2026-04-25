/* Linux lwIP netif bring-up: TUN/TAP backed by upstream tapif. */

#include "lwip_port_linux.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include "netif/tapif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile int s_tcpipReady;

static void lwipPortLinuxTcpipInitDone(void* arg)
{
    (void)arg;
    s_tcpipReady = 1;
}

err_t lwipPortLinuxStart(struct netif* netif,
                         const char* tapDevice,
                         u32_t ipAddrHbo,
                         u32_t netmaskHbo,
                         u32_t gatewayHbo)
{
    if (netif == NULL) {
        return ERR_ARG;
    }

    /* tapif consults this env variable to pick up an externally configured
     * device rather than running ifconfig itself - matches our setup script. */
    setenv("PRECONFIGURED_TAPIF", (tapDevice != NULL) ? tapDevice : "tap0", 1);

    tcpip_init(lwipPortLinuxTcpipInitDone, NULL);
    while (!s_tcpipReady) {
        sys_msleep(10);
    }

    ip4_addr_t ipaddr;
    ip4_addr_t netmask;
    ip4_addr_t gw;
    ipaddr.addr = lwip_htonl(ipAddrHbo);
    netmask.addr = lwip_htonl(netmaskHbo);
    gw.addr = lwip_htonl(gatewayHbo);

    if (netif_add(netif, &ipaddr, &netmask, &gw, NULL, tapif_init, tcpip_input) == NULL) {
        return ERR_IF;
    }
    netif_set_default(netif);
    netif_set_link_up(netif);
    netif_set_up(netif);

    printf("lwip_port_linux: netif up on %s with %u.%u.%u.%u/%u.%u.%u.%u gw %u.%u.%u.%u\n",
           (tapDevice != NULL) ? tapDevice : "tap0",
           ip4_addr1(&ipaddr),
           ip4_addr2(&ipaddr),
           ip4_addr3(&ipaddr),
           ip4_addr4(&ipaddr),
           ip4_addr1(&netmask),
           ip4_addr2(&netmask),
           ip4_addr3(&netmask),
           ip4_addr4(&netmask),
           ip4_addr1(&gw),
           ip4_addr2(&gw),
           ip4_addr3(&gw),
           ip4_addr4(&gw));
    fflush(stdout);
    return ERR_OK;
}
