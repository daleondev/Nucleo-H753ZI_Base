#ifndef LWIP_ARCH_PERF_H
#define LWIP_ARCH_PERF_H

#define PERF_START                                                                                           \
    do {                                                                                                     \
    } while (0)
#define PERF_STOP(x)                                                                                         \
    do {                                                                                                     \
        (void)(x);                                                                                           \
    } while (0)

#endif /* LWIP_ARCH_PERF_H */
