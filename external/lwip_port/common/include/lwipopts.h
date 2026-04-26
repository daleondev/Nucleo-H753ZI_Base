/*
 * lwIP configuration shared by every target in this project.
 *
 * NO_SYS=0: full sequential / socket API on top of a `sys_arch` (ThreadX).
 * IPv6 / PPP / SNMP / HTTPD are disabled - we only need TCP/UDP for OPC UA.
 */

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS 0
#define LWIP_SOCKET 1
#define LWIP_NETCONN 1
#define LWIP_DNS 1
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_RAW 1
#define LWIP_IPV4 1
#define LWIP_IPV6 0

#define LWIP_IGMP 1
#define LWIP_MULTICAST_TX_OPTIONS 1

/* open62541's eventloop creates a self-pipe via 127.0.0.1 sockets; lwIP needs
 * loopback support enabled for that to succeed. */
#define LWIP_HAVE_LOOPIF 1
#define LWIP_NETIF_LOOPBACK 1
#define LWIP_LOOPBACK_MAX_PBUFS 8

#define MEM_LIBC_MALLOC 0
#define MEM_ALIGNMENT 4
#define MEM_SIZE (32 * 1024)

#define MEMP_NUM_PBUF 24
/* Each UA_EventLoop instance allocates 2 self-pipe TCP PCBs in addition to
 * its listen / connect sockets. With one server + two clients (production +
 * bench) we need ~12 active PCBs, so bump from the default to leave headroom. */
#define MEMP_NUM_TCP_PCB 16
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define MEMP_NUM_TCP_SEG 32
#define MEMP_NUM_NETBUF 8
#define MEMP_NUM_NETCONN 16
#define MEMP_NUM_TCPIP_MSG_API 16
#define MEMP_NUM_TCPIP_MSG_INPKT 16
#define MEMP_NUM_SYS_TIMEOUT 16

#define PBUF_POOL_SIZE 16
#define PBUF_POOL_BUFSIZE 1536

#define TCP_MSS 1460
#define TCP_SND_BUF (8 * TCP_MSS)
#define TCP_WND (8 * TCP_MSS)

#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_DHCP 1
#define LWIP_AUTOIP 0

#define LWIP_SO_RCVTIMEO 1
#define LWIP_SO_SNDTIMEO 1
#define LWIP_SO_RCVBUF 1
#define SO_REUSE 1

#define LWIP_TCPIP_CORE_LOCKING 1
#define LWIP_COMPAT_MUTEX 0

#define TCPIP_THREAD_NAME "tcpip"
#define TCPIP_THREAD_STACKSIZE 4096
#define TCPIP_THREAD_PRIO 8
#define TCPIP_MBOX_SIZE 16
#define DEFAULT_THREAD_STACKSIZE 4096
#define DEFAULT_THREAD_PRIO 10
#define DEFAULT_RAW_RECVMBOX_SIZE 8
#define DEFAULT_UDP_RECVMBOX_SIZE 8
#define DEFAULT_TCP_RECVMBOX_SIZE 8
#define DEFAULT_ACCEPTMBOX_SIZE 8

#define LWIP_STATS 0
#define LWIP_DEBUG 0
/* On Linux we link against glibc which provides a TLS errno. Avoid lwIP's
 * private errno; on bare-metal STM32 newlib provides errno via reent. */
#define LWIP_ERRNO_STDINCLUDE 1

#define SYS_LIGHTWEIGHT_PROT 0

#define LWIP_RAND() ((u32_t)rand())

/* Use the platform's <sys/time.h> on Linux and newlib alike. lwIP's
 * private timeval clashes with newlib's <sys/select.h> on bare-metal. */
#define LWIP_TIMEVAL_PRIVATE 0

#endif /* LWIPOPTS_H */
