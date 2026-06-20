#ifndef BENCH_SERVER_CPU_LOAD_REPORTER_HPP
#define BENCH_SERVER_CPU_LOAD_REPORTER_HPP

#include <tx_api.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace bench
{
    /*
     * Always-on, low-overhead CPU-load reporter. Spawns one ThreadX thread
     * that, once per second, reads the global CpuLoad's idle counter, computes
     * the load over the last window and prints one line:
     *
     *   srv_cpu_load: t=<uptime_ms>  load=<+x.xx>%  observed=<N> iter/s  baseline=<N> iter/s
     *
     * Designed to be started in txMain immediately after globalCpuLoad has
     * been started and (ideally) calibrated. If calibration has not yet
     * happened the line still prints; the load percentage is simply
     * meaningless until baselineIterPerSec() is non-zero. Calibration may
     * be performed by some other thread later; the reporter notices on the
     * next tick.
     *
     * Lifecycle: stack-owned, start() spawns the reporter thread, the
     * destructor tears it down. The reporter is meant to live for the
     * application's lifetime.
     */
    class ServerCpuLoadReporter
    {
      public:
        ServerCpuLoadReporter() = default;
        ServerCpuLoadReporter(const ServerCpuLoadReporter&) = delete;
        ServerCpuLoadReporter& operator=(const ServerCpuLoadReporter&) = delete;
        ~ServerCpuLoadReporter();

        bool start();
        void stop();

      private:
        static void txEntry(ULONG argument);
        void run();

        TX_THREAD m_thread{};
        // 2 kB: only does counter reads, integer math, and printf. printf
        // via newlib pulls in ~1 kB of frame; 2 kB gives comfortable margin.
        alignas(8) std::array<std::byte, 2048> m_stack{};
        std::atomic<bool> m_running{ false };
    };
} // namespace bench

#endif // BENCH_SERVER_CPU_LOAD_REPORTER_HPP
