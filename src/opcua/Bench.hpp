#ifndef OPCUA_BENCH_HPP
#define OPCUA_BENCH_HPP

#include "../bench/CpuLoad.hpp"

#include <tx_api.h>

#include <array>
#include <atomic>
#include <cstddef>

namespace opcua
{
    /*
     * Benchmark harness: a dedicated ThreadX thread that, after connecting
     * its own private UA_Client to the given endpoint, runs a fixed set of
     * latency micro-benchmarks against the bench fixture nodes (see
     * BenchFixture.hpp) and prints a single report.
     *
     * The harness owns its own UA_Client so it does not race against the
     * application's production client (open62541 is not thread-safe with
     * UA_MULTITHREADING=0).
     *
     * Lifecycle mirrors Server / Client: stack-owned object, start() spawns
     * the thread, the destructor tears it down. The harness is one-shot —
     * the thread exits after printing the report.
     */
    class BenchHarness
    {
      public:
        BenchHarness() = default;
        BenchHarness(const BenchHarness&) = delete;
        BenchHarness& operator=(const BenchHarness&) = delete;
        ~BenchHarness();

        // endpointUrl: e.g. "opc.tcp://10.10.10.2:4840". Must outlive the call.
        bool start(const char* endpointUrl);

      private:
        static void txEntry(ULONG argument);
        void run();

        const char* m_endpointUrl{ nullptr };
        bench::CpuLoad m_cpuLoad{};
        TX_THREAD m_thread{};
        // 64 kB: open62541's client connect + decode path is stack hungry
        // on STM32 (8 kB chunk buffers + nested type decoders). 32 kB
        // overflowed during connect by ~2 kB; observed via GDB stack-error
        // handler trace (tx_thread_stack_ptr below stack_start).
        alignas(8) std::array<std::byte, 65536> m_stack{};
        std::atomic<bool> m_running{ false };
    };
} // namespace opcua

#endif // OPCUA_BENCH_HPP
