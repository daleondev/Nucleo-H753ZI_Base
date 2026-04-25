/*
 * Slim Linux TAP netif for the simulator.
 *
 * Replaces lwIP's contrib `tapif.c` so the receive thread can ride out the
 * SIGUSR/RT signals that the ThreadX-on-Linux scheduler relies on without
 * spamming perror("select: Interrupted system call") forever.
 *
 * The host side of `tap0` is provisioned by `scripts/setup_tap.sh` before
 * the application starts; we only attach to that pre-existing device.
 */

#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "lwip/snmp.h"
#include "lwip/stats.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "netif/etharp.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* The Linux ThreadX port serialises ALL threads on a single recursive
 * pthread mutex. Any blocking syscall made while we hold that mutex stalls
 * the whole RTOS. Drop the mutex around our blocking I/O calls and pick it
 * back up afterwards so the other ThreadX threads keep running.
 */
extern pthread_mutex_t _tx_linux_mutex;

static void txLinuxMutexDrop(int* recursive)
{
    *recursive = (int)_tx_linux_mutex.__data.__count;
    int n = *recursive;
    while (n > 0) {
        pthread_mutex_unlock(&_tx_linux_mutex);
        --n;
    }
}

static void txLinuxMutexReacquire(int recursive)
{
    while (recursive > 0) {
        pthread_mutex_lock(&_tx_linux_mutex);
        --recursive;
    }
}

#define TAPIF_DEV "/dev/net/tun"
#define TAPIF_DEFAULT "tap0"

struct tapif_state
{
    int fd;
};

static err_t tapifLowLevelOutput(struct netif* netif, struct pbuf* p);
static void tapifInputLoop(void* arg);

err_t tapif_init(struct netif* netif)
{
    struct tapif_state* state = (struct tapif_state*)mem_malloc(sizeof(*state));
    if (state == NULL) {
        return ERR_MEM;
    }
    netif->state = state;

    netif->name[0] = 't';
    netif->name[1] = 'p';
    netif->output = etharp_output;
    netif->linkoutput = tapifLowLevelOutput;
    netif->mtu = 1500;
    netif->hwaddr_len = 6;
    netif->hwaddr[0] = 0x02;
    netif->hwaddr[1] = 0x12;
    netif->hwaddr[2] = 0x34;
    netif->hwaddr[3] = 0x56;
    netif->hwaddr[4] = 0x78;
    netif->hwaddr[5] = 0xab;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;

    state->fd = open(TAPIF_DEV, O_RDWR);
    if (state->fd < 0) {
        perror("tapif_init: open " TAPIF_DEV);
        return ERR_IF;
    }

    const char* devName = getenv("PRECONFIGURED_TAPIF");
    if (devName == NULL || devName[0] == '\0') {
        devName = TAPIF_DEFAULT;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, devName, sizeof(ifr.ifr_name) - 1);
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (ioctl(state->fd, TUNSETIFF, &ifr) < 0) {
        perror("tapif_init: ioctl TUNSETIFF");
        close(state->fd);
        return ERR_IF;
    }

    netif_set_link_up(netif);

    /* Run the RX thread at a priority numerically higher than main so it
     * never preempts the bring-up sequence. ThreadX uses lower numbers for
     * higher priority. */
    sys_thread_new("tapif_rx", tapifInputLoop, netif, DEFAULT_THREAD_STACKSIZE, 20);
    return ERR_OK;
}

static err_t tapifLowLevelOutput(struct netif* netif, struct pbuf* p)
{
    struct tapif_state* state = (struct tapif_state*)netif->state;

    /* Linearise to a single buffer; pbuf chains are short for an MTU of 1500. */
    char buf[1600];
    if (p->tot_len > sizeof(buf)) {
        return ERR_IF;
    }
    pbuf_copy_partial(p, buf, p->tot_len, 0);

    ssize_t written;
    do {
        written = write(state->fd, buf, p->tot_len);
    } while (written < 0 && errno == EINTR);
    if (written != (ssize_t)p->tot_len) {
        return ERR_IF;
    }
    MIB2_STATS_NETIF_ADD(netif, ifoutoctets, (u32_t)written);
    return ERR_OK;
}

static void tapifInputLoop(void* arg)
{
    struct netif* netif = (struct netif*)arg;
    struct tapif_state* state = (struct tapif_state*)netif->state;

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(state->fd, &rfds);

        int recursive = 0;
        txLinuxMutexDrop(&recursive);
        int rv = select(state->fd + 1, &rfds, NULL, NULL, NULL);
        int err = errno;
        txLinuxMutexReacquire(recursive);
        errno = err;

        if (rv < 0) {
            if (errno == EINTR) {
                continue; /* ThreadX scheduler signal - just retry. */
            }
            perror("tapif_rx: select");
            sys_msleep(100);
            continue;
        }
        if (!FD_ISSET(state->fd, &rfds)) {
            continue;
        }

        char rxbuf[1600];
        ssize_t n;
        do {
            txLinuxMutexDrop(&recursive);
            n = read(state->fd, rxbuf, sizeof(rxbuf));
            err = errno;
            txLinuxMutexReacquire(recursive);
            errno = err;
        } while (n < 0 && errno == EINTR);
        if (n <= 0) {
            continue;
        }

        struct pbuf* p = pbuf_alloc(PBUF_RAW, (u16_t)n, PBUF_POOL);
        if (p == NULL) {
            continue;
        }
        if (pbuf_take(p, rxbuf, (u16_t)n) != ERR_OK) {
            pbuf_free(p);
            continue;
        }
        if (netif->input(p, netif) != ERR_OK) {
            pbuf_free(p);
        }
    }
}
