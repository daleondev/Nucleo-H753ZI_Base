#include "Bench.hpp"

#include "../bench/Stats.hpp"
#include "../bench/Stopwatch.hpp"
#include "../thread_diagnostics.hpp"
#include "BenchFixture.hpp"

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/types.h>

#include <cstdio>

namespace opcua
{
    namespace
    {
        constexpr UINT BENCH_THREAD_PRIO{ 14 };
        constexpr std::uint32_t WARMUP_ITERATIONS{ 32 };
#if defined(HAL_COMPAT_STM32)
        // Each synchronous read costs ~1 s on STM32 because the open62541
        // EventLoop is gated by its poll timeout. 256 samples already gives
        // a meaningful p99 and keeps the whole run under ~5 minutes.
        constexpr std::uint32_t BENCH_SAMPLE_COUNT{ 256 };
#else
        constexpr std::uint32_t BENCH_SAMPLE_COUNT{ 1024 };
#endif
        constexpr std::uint32_t CONNECT_RETRY_INTERVAL_MS{ 250 };
        constexpr std::uint32_t CONNECT_TIMEOUT_MS{ 15000 };
        constexpr std::uint32_t CPU_WINDOW_MS{ 1000 };

        // ThreadX passes a single ULONG (32-bit on x86_64-linux), too small
        // for a pointer. The application instantiates a single BenchHarness,
        // so we stash it in a file-scope pointer.
        BenchHarness* s_instance{ nullptr };

        ULONG msToTicks(std::uint32_t ms)
        {
            const ULONG ticks = (static_cast<ULONG>(ms) * TX_TIMER_TICKS_PER_SECOND + 999UL) / 1000UL;
            return ticks == 0 ? 1UL : ticks;
        }

        bool waitForConnection(UA_Client* uaClient, const char* endpointUrl, std::uint32_t timeoutMs)
        {
            std::uint32_t waited = 0;
            while (waited < timeoutMs) {
                const UA_StatusCode rc = UA_Client_connect(uaClient, endpointUrl);
                if (rc == UA_STATUSCODE_GOOD) {
                    return true;
                }
                std::printf("bench: client connect failed (0x%08x), retrying...\n",
                            static_cast<unsigned>(rc));
                std::fflush(stdout);
                tx_thread_sleep(msToTicks(CONNECT_RETRY_INTERVAL_MS));
                waited += CONNECT_RETRY_INTERVAL_MS;
            }
            return false;
        }

        bool runReadRttScenario(UA_Client* uaClient,
                                std::uint32_t warmup,
                                std::uint32_t samples,
                                bench::Stats& stats)
        {
            const UA_NodeId target = UA_NODEID_NUMERIC(BENCH_NAMESPACE_INDEX, BENCH_NODEID_SCALAR_U64);

            UA_Variant value;
            UA_Variant_init(&value);

            for (std::uint32_t i = 0; i < warmup; ++i) {
                UA_Variant_clear(&value);
                UA_Variant_init(&value);
                const UA_StatusCode rc = UA_Client_readValueAttribute(uaClient, target, &value);
                if (rc != UA_STATUSCODE_GOOD) {
                    UA_Variant_clear(&value);
                    std::printf("bench: warm-up read failed (0x%08x)\n", static_cast<unsigned>(rc));
                    std::fflush(stdout);
                    return false;
                }
            }

            for (std::uint32_t i = 0; i < samples; ++i) {
                UA_Variant_clear(&value);
                UA_Variant_init(&value);
                bench::Stopwatch sw;
                const UA_StatusCode rc = UA_Client_readValueAttribute(uaClient, target, &value);
                const std::uint64_t elapsedNs = sw.elapsedNs();
                if (rc != UA_STATUSCODE_GOOD) {
                    UA_Variant_clear(&value);
                    std::printf("bench: timed read %u failed (0x%08x)\n",
                                static_cast<unsigned>(i),
                                static_cast<unsigned>(rc));
                    std::fflush(stdout);
                    return false;
                }
                stats.add(elapsedNs);
            }
            UA_Variant_clear(&value);
            return true;
        }

        void printLatencyTable(const char* scenarioName, const bench::Stats::Result& r)
        {
            const double toUs = 1.0 / 1000.0;
            std::printf("┌──────────────────────┬──────────┬──────────┬──────────┬──────────┬──────────┬─────"
                        "─────┬──────────┬──────────┐\n");
            std::printf("│ Scenario             │  Samples │ Min [us] │ Mean [us]│ Stdev[us]│ p50 [us] │ p95 "
                        "[us] │ p99 [us] │ Max [us] │\n");
            std::printf("├──────────────────────┼──────────┼──────────┼──────────┼──────────┼──────────┼─────"
                        "─────┼──────────┼──────────┤\n");
            std::printf("│ %-20s │ %8u │ %8.1f │ %8.1f │ %8.1f │ %8.1f │ %8.1f │ %8.1f │ %8.1f │\n",
                        scenarioName,
                        static_cast<unsigned>(r.count),
                        static_cast<double>(r.minNs) * toUs,
                        r.meanNs * toUs,
                        r.stddevNs * toUs,
                        static_cast<double>(r.p50Ns) * toUs,
                        static_cast<double>(r.p95Ns) * toUs,
                        static_cast<double>(r.p99Ns) * toUs,
                        static_cast<double>(r.maxNs) * toUs);
            std::printf("└──────────────────────┴──────────┴──────────┴──────────┴──────────┴──────────┴─────"
                        "─────┴──────────┴──────────┘\n");
            std::fflush(stdout);
        }
    } // namespace

    BenchHarness::~BenchHarness()
    {
        if (m_running.exchange(false, std::memory_order_acq_rel)) {
            tx_thread_terminate(&m_thread);
            tx_thread_delete(&m_thread);
        }
        m_cpuLoad.stop();
        s_instance = nullptr;
    }

    bool BenchHarness::start(const char* endpointUrl)
    {
        if (m_running.load(std::memory_order_acquire) || endpointUrl == nullptr || endpointUrl[0] == '\0') {
            return false;
        }
        // Enable the cycle-counter on STM32 (no-op on Linux).
        bench::Stopwatch::initOnce();
        m_endpointUrl = endpointUrl;
        if (!m_cpuLoad.start()) {
            std::printf("bench: failed to start CPU-load idle thread\n");
            std::fflush(stdout);
            return false;
        }
        m_running.store(true, std::memory_order_release);
        s_instance = this;
        const UINT status = tx_thread_create(&m_thread,
                                             const_cast<CHAR*>("bench_main"),
                                             BenchHarness::txEntry,
                                             0UL,
                                             m_stack.data(),
                                             static_cast<ULONG>(m_stack.size()),
                                             BENCH_THREAD_PRIO,
                                             BENCH_THREAD_PRIO,
                                             TX_NO_TIME_SLICE,
                                             TX_AUTO_START);
        if (status != TX_SUCCESS) {
            m_running.store(false, std::memory_order_release);
            m_cpuLoad.stop();
            return false;
        }
        return true;
    }

    void BenchHarness::txEntry(ULONG argument)
    {
        (void)argument;
        if (s_instance != nullptr) {
            s_instance->run();
        }
    }

    void BenchHarness::run()
    {
        if (m_endpointUrl == nullptr) {
            return;
        }

        UA_Client* uaClient = UA_Client_new();
        if (uaClient == nullptr) {
            std::printf("bench: UA_Client_new failed\n");
            std::fflush(stdout);
            m_running.store(false, std::memory_order_release);
            return;
        }
        if (UA_ClientConfig_setDefault(UA_Client_getConfig(uaClient)) != UA_STATUSCODE_GOOD) {
            UA_Client_delete(uaClient);
            std::printf("bench: UA_ClientConfig_setDefault failed\n");
            std::fflush(stdout);
            m_running.store(false, std::memory_order_release);
            return;
        }

#if defined(HAL_COMPAT_STM32)
        // Match the server-side 8 kB chunk buffer; default 64 kB exhausts heap.
        UA_ClientConfig* benchConfig = UA_Client_getConfig(uaClient);
        benchConfig->localConnectionConfig.sendBufferSize = 8192;
        benchConfig->localConnectionConfig.recvBufferSize = 8192;
#endif

        std::printf("bench: connecting to %s...\n", m_endpointUrl);
        std::fflush(stdout);
        if (!waitForConnection(uaClient, m_endpointUrl, CONNECT_TIMEOUT_MS)) {
            std::printf("bench: failed to connect within %u ms, aborting\n",
                        static_cast<unsigned>(CONNECT_TIMEOUT_MS));
            std::fflush(stdout);
            UA_Client_delete(uaClient);
            m_running.store(false, std::memory_order_release);
            return;
        }
        std::printf("bench: connected, calibrating CPU baseline (%u ms)...\n",
                    static_cast<unsigned>(CPU_WINDOW_MS));
        std::fflush(stdout);

        const bool calibrated = m_cpuLoad.calibrate(CPU_WINDOW_MS);
        if (!calibrated) {
            std::printf("bench: CPU-load calibration produced no samples; CPU readings will be unreliable\n");
            std::fflush(stdout);
        }

        bench::Stats stats;
        const bool ok = runReadRttScenario(uaClient, WARMUP_ITERATIONS, BENCH_SAMPLE_COUNT, stats);
        const double loadFraction = calibrated ? m_cpuLoad.sample(CPU_WINDOW_MS) : 0.0;

        UA_Client_disconnect(uaClient);
        UA_Client_delete(uaClient);

        if (!ok) {
            std::printf("bench: scenario aborted, no report.\n");
            std::fflush(stdout);
            m_running.store(false, std::memory_order_release);
            return;
        }

        const auto result = stats.compute();
        std::printf("\n=== OPC UA client benchmark report ===\n");
        printLatencyTable("scalar_u64 read RTT", result);

        if (calibrated) {
            std::printf("cpu_load: post-bench=%.1f%% (baseline=%llu iter/s)\n",
                        loadFraction * 100.0,
                        static_cast<unsigned long long>(m_cpuLoad.baselineIterPerSec()));
        }
        else {
            std::printf("cpu_load: unavailable (calibration failed)\n");
        }
#if defined(HAL_COMPAT_LINUX)
        std::printf("           note: CPU-load on Linux is approximate "
                    "(_tx_linux_mutex serialises threads).\n");
#endif
        std::fflush(stdout);

        thread_diagnostics::printAll();

        std::printf("[BENCH-DONE]\n");
        std::fflush(stdout);

        m_running.store(false, std::memory_order_release);
    }
} // namespace opcua
