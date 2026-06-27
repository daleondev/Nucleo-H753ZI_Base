#pragma once

#include <stdint.h>

/* STM32H7 Cortex-M7 data-cache line size. Keep every DMA buffer isolated on
 * cache-line boundaries even though ETH_BUFFER_RAM is configured non-cacheable. */
#define ETH_DMA_BUFFER_ALIGNMENT 32U
#define ETH_DMA_FRAME_BUFFER_SIZE 1536U

#if defined(__GNUC__)
#define ETH_DMA_BUFFER_ATTRIBUTE \
    __attribute__((section(".EthBufferSection"), aligned(ETH_DMA_BUFFER_ALIGNMENT)))
#else
#error "Define ETH_DMA_BUFFER_ATTRIBUTE for this toolchain"
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t __eth_buffer_start__[];
extern uint8_t __eth_buffer_end__[];

#ifdef __cplusplus
}
#endif
