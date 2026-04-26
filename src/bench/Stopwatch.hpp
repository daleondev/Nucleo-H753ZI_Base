#ifndef BENCH_STOPWATCH_HPP
#define BENCH_STOPWATCH_HPP

#include <chrono>
#include <cstdint>

#if defined(HAL_COMPAT_STM32)
#include "stm32h7xx.h" // CoreDebug, DWT, SystemCoreClock
#endif

namespace bench
{
#if defined(HAL_COMPAT_STM32)
    /*
     * STM32 Stopwatch backed by the Cortex-M DWT cycle counter.
     *
     * std::chrono::steady_clock on bare-metal newlib resolves to
     * _gettimeofday() (which is unimplemented and returns garbage), so we
     * cannot rely on it. DWT->CYCCNT runs at the core clock and provides
     * 1-cycle resolution. The 32-bit counter wraps every ~9 s at 480 MHz,
     * which is well above the longest single read RTT we measure.
     */
    class Stopwatch
    {
      public:
        // Enable the DWT cycle counter once at startup; safe to call from
        // any context. Returns false if the core does not implement DWT.
        static bool initOnce() noexcept
        {
            if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0) {
                CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
            }
            DWT->LAR = 0xC5ACCE55UL; // unlock (Cortex-M7); harmless on M4
            DWT->CYCCNT = 0;
            DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
            return (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0;
        }

        Stopwatch() noexcept
          : m_start{ DWT->CYCCNT }
        {
        }

        void reset() noexcept { m_start = DWT->CYCCNT; }

        std::uint64_t elapsedNs() const noexcept
        {
            const std::uint32_t now = DWT->CYCCNT;
            const std::uint32_t cycles = static_cast<std::uint32_t>(now - m_start);
            // ns = cycles * 1e9 / SystemCoreClock; do it in 64 bit to avoid overflow.
            return (static_cast<std::uint64_t>(cycles) * 1000000000ULL) /
                   static_cast<std::uint64_t>(SystemCoreClock);
        }

      private:
        std::uint32_t m_start{ 0 };
    };
#else
    /*
     * Linux Stopwatch backed by std::chrono::steady_clock (CLOCK_MONOTONIC).
     */
    class Stopwatch
    {
      public:
        using clock = std::chrono::steady_clock;

        static bool initOnce() noexcept { return true; }

        Stopwatch() noexcept
          : m_start{ clock::now() }
        {
        }

        void reset() noexcept { m_start = clock::now(); }

        std::uint64_t elapsedNs() const noexcept
        {
            const auto delta = clock::now() - m_start;
            return static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count());
        }

      private:
        clock::time_point m_start;
    };
#endif
} // namespace bench

#endif // BENCH_STOPWATCH_HPP
