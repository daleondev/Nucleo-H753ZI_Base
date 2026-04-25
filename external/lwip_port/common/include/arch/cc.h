/* lwIP compiler/architecture abstraction - shared across targets. */

#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if !defined(__arm__) && !defined(__thumb__)
/* On hosted POSIX, pull in <sys/types.h> early so ssize_t is the same
 * `long int` definition the rest of glibc uses. lwIP arch.h then skips
 * its fallback `typedef int ssize_t`. We force SSIZE_MAX so the check
 * succeeds even on toolchains where <limits.h> alone does not define it. */
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef SSIZE_MAX
#define SSIZE_MAX __SSIZE_MAX__
#endif
#else
/* Bare-metal newlib: provide struct timeval / fd_set so lwIP and
 * upstream open62541 lwIP eventloop agree on the same definitions. */
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#endif

#define LWIP_NO_INTTYPES_H 0

/* On glibc <endian.h> already defines BYTE_ORDER/LITTLE_ENDIAN. */
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

#define LWIP_PLATFORM_DIAG(x)                                                                                \
    do {                                                                                                     \
        printf x;                                                                                            \
    } while (0)

#ifdef NDEBUG
#define LWIP_PLATFORM_ASSERT(x) ((void)0)
#else
#define LWIP_PLATFORM_ASSERT(x)                                                                              \
    do {                                                                                                     \
        printf("lwIP assertion: %s at %s:%d\n", x, __FILE__, __LINE__);                                      \
        fflush(NULL);                                                                                        \
        abort();                                                                                             \
    } while (0)
#endif

#endif /* LWIP_ARCH_CC_H */
