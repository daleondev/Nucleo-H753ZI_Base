#ifndef BENCH_CPULOAD_HPP
#define BENCH_CPULOAD_HPP

#include <tx_api.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace bench
{
    /*
     * Coarse CPU-load estimator built around a single low-priority ThreadX
     * thread that does nothing but increment a counter. The ratio between
     * the count observed during a baseline window (no traffic) and during a
     * load window approximates the fraction of CPU stolen by other work.
     *
     * On Linux the global _tx_linux_mutex serialises all ThreadX threads,
     * so this measurement is approximate. On STM32 it tracks real idle time
     * fairly well as long as no thread of the same priority is running.
     */
    class CpuLoad
    {
      public:
        bool start();                           // create + start the idle thread
        void stop();                            // terminate + delete the idle thread
        bool calibrate(std::uint32_t windowMs); // sample baseline (no load)
        // Returns load in [0, 1]. Negative results are clamped to 0.
        double sample(std::uint32_t windowMs);

        std::uint64_t baselineIterPerSec() const noexcept { return m_baselineIterPerSec; }

      private:
        static void txEntry(ULONG argument);
        void run();

        std::uint32_t snapshotCounter() const noexcept { return m_counter.load(std::memory_order_relaxed); }

        TX_THREAD m_thread{};
        alignas(8) std::array<std::byte, 1024> m_stack{};
        // 32-bit counter avoids __atomic_load_8/__atomic_fetch_add_8 helper
        // calls on Cortex-M (the toolchain doesn't lower these to LDREXD/STREXD).
        // The counter wraps; deltas over short windows fit comfortably in 32 bits.
        std::atomic<std::uint32_t> m_counter{ 0 };
        std::atomic<bool> m_running{ false };
        std::uint64_t m_baselineIterPerSec{ 0 };
    };
} // namespace bench

#endif // BENCH_CPULOAD_HPP
