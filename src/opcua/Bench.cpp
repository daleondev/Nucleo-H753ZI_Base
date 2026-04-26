#include "Bench.hpp"

#include "../bench/Stats.hpp"
#include "../bench/Stopwatch.hpp"
#include "../thread_diagnostics.hpp"
#include "BenchFixture.hpp"

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_subscriptions.h>
#include <open62541/types.h>

#include <cstdio>
#if defined(HAL_COMPAT_STM32)
#include <malloc.h> // mallinfo for heap diagnostics on newlib
#endif

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
        // Monitored-items sweep: each sample is gated by the publishing
        // interval (~50 ms) so 32 samples × N values ≈ a few seconds extra.
        // N is capped at 16 because creating 32+ MonitoredItems exhausts
        // the server-side heap (createDataChanges then returns
        // BadOutOfMemory). With ~17 kB free heap on the server during
        // steady-state, each MI costs ~400 B in queue + bookkeeping.
        constexpr std::uint32_t MON_SAMPLE_COUNT{ 32 };
        constexpr std::uint32_t MON_COUNTS[]{ 1, 4, 8, 16 };
#else
        constexpr std::uint32_t BENCH_SAMPLE_COUNT{ 1024 };
        constexpr std::uint32_t MON_SAMPLE_COUNT{ 64 };
        constexpr std::uint32_t MON_COUNTS[]{ 1, 10, 50 };
#endif
        constexpr std::uint32_t CONNECT_RETRY_INTERVAL_MS{ 250 };
        constexpr std::uint32_t CONNECT_TIMEOUT_MS{ 60000 };
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
                // Reset the client back to a fully-disconnected state. Without
                // this, repeated UA_Client_connect calls on the same client
                // can wedge in ChannelState=Connected/SessionState=Closed and
                // every subsequent connect returns BadOutOfMemory while the
                // server's stale SecureChannel waits to time out.
                UA_Client_disconnect(uaClient);
                tx_thread_sleep(msToTicks(CONNECT_RETRY_INTERVAL_MS));
                waited += CONNECT_RETRY_INTERVAL_MS;
            }
            return false;
        }

        bool runReadRttScenario(UA_Client* uaClient,
                                std::uint32_t warmup,
                                std::uint32_t samples,
                                bench::Stats& stats,
                                bench::CpuLoad& cpuLoad,
                                double& outLoadDuringScenario,
                                std::uint64_t& outScenarioElapsedNs,
                                std::uint32_t& outIdleDelta)
        {
            outLoadDuringScenario = 0.0;
            outScenarioElapsedNs = 0;
            outIdleDelta = 0;

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

            // Snapshot the idle counter right before the timed loop so CPU
            // load is measured *during* the actual benchmark traffic.
            //
            // The scenario wall clock is accumulated as the sum of the
            // per-iteration stopwatches rather than a single Stopwatch
            // spanning the whole loop. On STM32 we use DWT->CYCCNT (32-bit)
            // for nanosecond timing, which wraps every ~9 s at 480 MHz; a
            // 256-sample run takes ~25 s and would overflow a single
            // stopwatch. Each iteration is ~100 ms, well under the wrap
            // window, and the gap between iterations is negligible relative
            // to the read RTT, so the sum is an accurate approximation of
            // the scenario wall-clock time.
            const std::uint32_t idleBefore = cpuLoad.counter();
            std::uint64_t scenarioElapsedNs = 0;

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
                scenarioElapsedNs += elapsedNs;
            }

            const std::uint32_t idleAfter = cpuLoad.counter();
            const std::uint32_t idleDelta = static_cast<std::uint32_t>(idleAfter - idleBefore);

            outScenarioElapsedNs = scenarioElapsedNs;
            outIdleDelta = idleDelta;
            outLoadDuringScenario = cpuLoad.loadOver(idleDelta, scenarioElapsedNs);

            UA_Variant_clear(&value);
            return true;
        }

        void printLatencyTableHeader()
        {
            std::printf("┌──────────────────────┬──────────┬──────────┬──────────┬──────────┬──────────┬─────"
                        "─────┬──────────┬──────────┐\n");
            std::printf("│ Scenario             │  Samples │ Min [us] │ Mean [us]│ Stdev[us]│ p50 [us] │ p95 "
                        "[us] │ p99 [us] │ Max [us] │\n");
            std::printf("├──────────────────────┼──────────┼──────────┼──────────┼──────────┼──────────┼─────"
                        "─────┼──────────┼──────────┤\n");
        }

        void printLatencyTableRow(const char* scenarioName, const bench::Stats::Result& r)
        {
            const double toUs = 1.0 / 1000.0;
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
        }

        void printLatencyTableFooter()
        {
            std::printf("└──────────────────────┴──────────┴──────────┴──────────┴──────────┴──────────┴─────"
                        "─────┴──────────┴──────────┘\n");
            std::fflush(stdout);
        }

        // Print newlib heap usage: arena (sbrk-grown total), in-use, free,
        // largest-free-block. The largest-free-block is the most useful
        // signal for fragmentation: createDataChanges fails if no
        // contiguous 8 kB chunk is available, even if total free is large.
        void printHeapUsage(const char* label)
        {
#if defined(HAL_COMPAT_STM32)
            struct mallinfo mi = mallinfo();
            std::printf("heap[%s]: arena=%u used=%u free=%u largest_free=%u keepcost=%u\n",
                        label,
                        static_cast<unsigned>(mi.arena),
                        static_cast<unsigned>(mi.uordblks),
                        static_cast<unsigned>(mi.fordblks),
                        static_cast<unsigned>(mi.usmblks),
                        static_cast<unsigned>(mi.keepcost));
            std::fflush(stdout);
#else
            (void)label;
#endif
        }

        // -- Monitored-item scenario state ----------------------------------
        //
        // The data-change callback is invoked from inside
        // UA_Client_run_iterate on the bench thread itself, so the state
        // can be plain (non-atomic). Only the first monitored item carries
        // the callback; the rest are silent registrations whose only effect
        // is to load the server's publish bookkeeping with N items.
        struct MonItemState
        {
            UA_UInt64 latestValue{ 0 };
            bool received{ false };
        };
        MonItemState s_monState{};

        void monDataChangeCb(UA_Client* /*client*/,
                             UA_UInt32 /*subId*/,
                             void* /*subContext*/,
                             UA_UInt32 /*monId*/,
                             void* /*monContext*/,
                             UA_DataValue* value)
        {
            if (value == nullptr || !value->hasValue) {
                return;
            }
            if (!UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_UINT64])) {
                return;
            }
            s_monState.latestValue = *static_cast<UA_UInt64*>(value->value.data);
            s_monState.received = true;
        }

        // Run a single round of the monitored-items scenario for the given
        // monitored-item count. Creates a subscription, registers monCount
        // monitored items on bench.mon_u64.[0..monCount), then for `samples`
        // iterations writes a fresh sequence number to bench.mon_u64.0 and
        // measures the time from write-returned to the matching DataChange
        // notification arriving at the client callback. Tears down the
        // subscription before returning.
        bool runMonitoredItemsScenario(UA_Client* uaClient,
                                       bench::CpuLoad& cpuLoad,
                                       std::uint32_t monCount,
                                       std::uint32_t samples,
                                       bench::Stats& stats,
                                       double& outLoadDuringScenario,
                                       std::uint64_t& outScenarioElapsedNs,
                                       std::uint32_t& outIdleDelta)
        {
            outLoadDuringScenario = 0.0;
            outScenarioElapsedNs = 0;
            outIdleDelta = 0;
            if (monCount == 0 || monCount > BENCH_MON_NODE_COUNT) {
                return false;
            }

            UA_CreateSubscriptionRequest subReq = UA_CreateSubscriptionRequest_default();
            subReq.requestedPublishingInterval = 50.0;
            subReq.requestedMaxKeepAliveCount = 100;
            subReq.requestedLifetimeCount = 10000;

            UA_CreateSubscriptionResponse subResp =
              UA_Client_Subscriptions_create(uaClient, subReq, nullptr, nullptr, nullptr);
            const UA_StatusCode subRc = subResp.responseHeader.serviceResult;
            const UA_UInt32 subId = subResp.subscriptionId;
            UA_CreateSubscriptionResponse_clear(&subResp);
            if (subRc != UA_STATUSCODE_GOOD) {
                std::printf("bench: subscription create failed (0x%08x)\n", static_cast<unsigned>(subRc));
                std::fflush(stdout);
                return false;
            }

            // Heap-allocate the createDataChanges arrays sized to monCount.
            // Putting them on the bench-thread stack would add ~10 kB at
            // BENCH_MON_NODE_COUNT=64 (each UA_MonitoredItemCreateRequest is
            // ~150 B), which overflows the 16 kB ThreadX stack.
            UA_MonitoredItemCreateRequest* items = static_cast<UA_MonitoredItemCreateRequest*>(
              UA_Array_new(monCount, &UA_TYPES[UA_TYPES_MONITOREDITEMCREATEREQUEST]));
            UA_Client_DataChangeNotificationCallback* cbs =
              static_cast<UA_Client_DataChangeNotificationCallback*>(
                UA_calloc(monCount, sizeof(UA_Client_DataChangeNotificationCallback)));
            void** contexts = static_cast<void**>(UA_calloc(monCount, sizeof(void*)));
            UA_Client_DeleteMonitoredItemCallback* delCbs =
              static_cast<UA_Client_DeleteMonitoredItemCallback*>(
                UA_calloc(monCount, sizeof(UA_Client_DeleteMonitoredItemCallback)));
            if (items == nullptr || cbs == nullptr || contexts == nullptr || delCbs == nullptr) {
                std::printf("bench: OOM allocating monitored-items arrays (N=%u)\n",
                            static_cast<unsigned>(monCount));
                std::fflush(stdout);
                UA_Array_delete(items, monCount, &UA_TYPES[UA_TYPES_MONITOREDITEMCREATEREQUEST]);
                UA_free(cbs);
                UA_free(contexts);
                UA_free(delCbs);
                UA_Client_Subscriptions_deleteSingle(uaClient, subId);
                return false;
            }

            for (std::uint32_t i = 0; i < monCount; ++i) {
                const UA_NodeId n = UA_NODEID_NUMERIC(BENCH_NAMESPACE_INDEX, BENCH_NODEID_MON_BASE + i);
                items[i] = UA_MonitoredItemCreateRequest_default(n);
                items[i].requestedParameters.samplingInterval = 0; // server-paced
                items[i].requestedParameters.queueSize = 1;
                items[i].requestedParameters.discardOldest = true;
            }
            // Only the first item drives latency measurement; the rest are
            // silent registrations that load the server's publish path.
            cbs[0] = monDataChangeCb;

            UA_CreateMonitoredItemsRequest miReq;
            UA_CreateMonitoredItemsRequest_init(&miReq);
            miReq.subscriptionId = subId;
            miReq.timestampsToReturn = UA_TIMESTAMPSTORETURN_NEITHER;
            miReq.itemsToCreate = items;
            miReq.itemsToCreateSize = monCount;

            UA_CreateMonitoredItemsResponse miResp =
              UA_Client_MonitoredItems_createDataChanges(uaClient, miReq, contexts, cbs, delCbs);
            const UA_StatusCode miRc = miResp.responseHeader.serviceResult;
            const std::size_t resultsSize = miResp.resultsSize;
            bool allOk = (miRc == UA_STATUSCODE_GOOD) && (resultsSize == monCount);
            if (allOk) {
                for (std::size_t i = 0; i < resultsSize; ++i) {
                    if (miResp.results[i].statusCode != UA_STATUSCODE_GOOD) {
                        allOk = false;
                        break;
                    }
                }
            }
            UA_CreateMonitoredItemsResponse_clear(&miResp);
            UA_Array_delete(items, monCount, &UA_TYPES[UA_TYPES_MONITOREDITEMCREATEREQUEST]);
            UA_free(cbs);
            UA_free(contexts);
            UA_free(delCbs);
            if (!allOk) {
                std::printf("bench: createDataChanges failed (svc=0x%08x, results=%u/%u)\n",
                            static_cast<unsigned>(miRc),
                            static_cast<unsigned>(resultsSize),
                            static_cast<unsigned>(monCount));
                std::fflush(stdout);
                UA_Client_Subscriptions_deleteSingle(uaClient, subId);
                return false;
            }

            // Drain initial-value notifications that the server publishes
            // immediately after createDataChanges, so they do not pollute
            // the first sample's latency measurement.
            for (std::uint32_t i = 0; i < 10; ++i) {
                UA_Client_run_iterate(uaClient, 20);
            }
            s_monState.received = false;
            s_monState.latestValue = 0;

            const UA_NodeId firstNode = UA_NODEID_NUMERIC(BENCH_NAMESPACE_INDEX, BENCH_NODEID_MON_BASE);
            const std::uint32_t idleBefore = cpuLoad.counter();
            std::uint64_t totalElapsedNs = 0;
            std::uint32_t timeouts = 0;
            constexpr std::uint64_t TIMEOUT_NS = 2ULL * 1000ULL * 1000ULL * 1000ULL; // 2 s

            // NOTE: We deliberately write only to the FIRST monitored
            // node, not to all N. The earlier batched-write variant
            // (one WriteValue per monitored item, reusing a stack-local
            // UA_UInt64 with UA_VARIANT_DATA_NODELETE) corrupted the
            // newlib heap on STM32, causing HardFaults a few hundred
            // iterations in. The single-write keeps memory traffic
            // predictable; the cost is that publish-side load only
            // scales with the number of registered MIs (server still
            // has to evaluate sampling/publishing for all N), not with
            // the number of *changing* items per publish cycle.

            for (std::uint32_t i = 0; i < samples; ++i) {
                const UA_UInt64 seq = static_cast<UA_UInt64>(i + 1);
                UA_Variant v;
                UA_Variant_init(&v);
                if (UA_Variant_setScalarCopy(&v, &seq, &UA_TYPES[UA_TYPES_UINT64]) != UA_STATUSCODE_GOOD) {
                    UA_Client_Subscriptions_deleteSingle(uaClient, subId);
                    return false;
                }

                s_monState.received = false;
                s_monState.latestValue = 0;

                bench::Stopwatch sw;
                const UA_StatusCode wrc = UA_Client_writeValueAttribute(uaClient, firstNode, &v);
                UA_Variant_clear(&v);
                if (wrc != UA_STATUSCODE_GOOD) {
                    std::printf("bench: mon write %u failed (0x%08x)\n",
                                static_cast<unsigned>(i),
                                static_cast<unsigned>(wrc));
                    std::fflush(stdout);
                    UA_Client_Subscriptions_deleteSingle(uaClient, subId);
                    return false;
                }

                while (!(s_monState.received && s_monState.latestValue == seq)) {
                    UA_Client_run_iterate(uaClient, 5);
                    if (sw.elapsedNs() > TIMEOUT_NS) {
                        break;
                    }
                }
                const std::uint64_t elapsedNs = sw.elapsedNs();
                totalElapsedNs += elapsedNs;
                if (s_monState.received && s_monState.latestValue == seq) {
                    stats.add(elapsedNs);
                }
                else {
                    ++timeouts;
                }
            }

            const std::uint32_t idleAfter = cpuLoad.counter();
            const std::uint32_t idleDelta = static_cast<std::uint32_t>(idleAfter - idleBefore);
            outScenarioElapsedNs = totalElapsedNs;
            outIdleDelta = idleDelta;
            outLoadDuringScenario = cpuLoad.loadOver(idleDelta, totalElapsedNs);

            UA_Client_Subscriptions_deleteSingle(uaClient, subId);

            if (timeouts > 0) {
                std::printf("bench: monitored-items N=%u timeouts=%u/%u\n",
                            static_cast<unsigned>(monCount),
                            static_cast<unsigned>(timeouts),
                            static_cast<unsigned>(samples));
                std::fflush(stdout);
            }
            return true;
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

        printHeapUsage("bench-start");
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
        // Default per-request timeout (5 s) is too tight on STM32 because
        // each round-trip is gated by the open62541 EventLoop's 100 ms poll
        // tick. The full HEL+OPN+GetEndpoints+CreateSession+Activate handshake
        // can take ~10 s on STM32. Without this the connect aborts mid-handshake
        // with BadTimeout even though the server is responding correctly.
        benchConfig->timeout = 30000;
#endif

        // Calibrate the CPU-load baseline AFTER the client has connected
        // and the system has had a brief moment to settle. The baseline
        // represents "no client traffic, but stack fully connected" — that
        // way the scenario load reflects only the per-request work.
        // calibrate() takes multiple samples internally and uses the
        // most-idle one to filter out transient activity bursts.
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
        std::printf("bench: connected, settling before CPU calibration...\n");
        std::fflush(stdout);
        tx_thread_sleep(msToTicks(500));

        std::printf("bench: calibrating CPU baseline (%u samples × %u ms)...\n",
                    4u,
                    static_cast<unsigned>(CPU_WINDOW_MS));
        std::fflush(stdout);
        const bool calibrated = m_cpuLoad.calibrate(CPU_WINDOW_MS);
        if (calibrated) {
            std::printf("bench: baseline = %llu iter/s (most-idle of 4 samples)\n",
                        static_cast<unsigned long long>(m_cpuLoad.baselineIterPerSec()));
            std::fflush(stdout);
        }
        else {
            std::printf("bench: CPU-load calibration produced no samples; CPU readings will be unreliable\n");
            std::fflush(stdout);
        }

        bench::Stats stats;
        double loadDuringScenario = 0.0;
        std::uint64_t scenarioElapsedNs = 0;
        std::uint32_t scenarioIdleDelta = 0;
        printHeapUsage("pre-readrtt");
        const bool ok = runReadRttScenario(uaClient,
                                           WARMUP_ITERATIONS,
                                           BENCH_SAMPLE_COUNT,
                                           stats,
                                           m_cpuLoad,
                                           loadDuringScenario,
                                           scenarioElapsedNs,
                                           scenarioIdleDelta);
        printHeapUsage("post-readrtt");
        // Idle / post-bench load (no traffic): a single short window after
        // the read loop. Useful as a sanity check that the system returns
        // to ~0 % when the bench is no longer driving requests.
        const double loadIdle = calibrated ? m_cpuLoad.sample(CPU_WINDOW_MS) : 0.0;

        // -- Monitored-items sweep -----------------------------------------
        // Run only if the read scenario succeeded; this reuses the same
        // UA_Client (subscriptions live on a session, but we have not
        // disconnected yet).
        constexpr std::size_t MON_COUNTS_N = sizeof(MON_COUNTS) / sizeof(MON_COUNTS[0]);
        bench::Stats monStats[MON_COUNTS_N];
        double monLoad[MON_COUNTS_N]{};
        std::uint64_t monElapsedNs[MON_COUNTS_N]{};
        std::uint32_t monIdleDelta[MON_COUNTS_N]{};
        bool monOk[MON_COUNTS_N]{};
        if (ok) {
            for (std::size_t k = 0; k < MON_COUNTS_N; ++k) {
                std::printf("bench: monitored-items scenario N=%u (%u samples)...\n",
                            static_cast<unsigned>(MON_COUNTS[k]),
                            static_cast<unsigned>(MON_SAMPLE_COUNT));
                std::fflush(stdout);
                char heapLabel[24];
                std::snprintf(
                  heapLabel, sizeof(heapLabel), "pre-mon-N=%u", static_cast<unsigned>(MON_COUNTS[k]));
                printHeapUsage(heapLabel);
                monOk[k] = runMonitoredItemsScenario(uaClient,
                                                     m_cpuLoad,
                                                     MON_COUNTS[k],
                                                     MON_SAMPLE_COUNT,
                                                     monStats[k],
                                                     monLoad[k],
                                                     monElapsedNs[k],
                                                     monIdleDelta[k]);
                std::snprintf(
                  heapLabel, sizeof(heapLabel), "post-mon-N=%u", static_cast<unsigned>(MON_COUNTS[k]));
                printHeapUsage(heapLabel);
            }
        }

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
        printLatencyTableHeader();
        printLatencyTableRow("scalar_u64 read RTT", result);
        for (std::size_t k = 0; k < MON_COUNTS_N; ++k) {
            if (!monOk[k]) {
                continue;
            }
            char rowName[24];
            std::snprintf(rowName, sizeof(rowName), "mon_items N=%u", static_cast<unsigned>(MON_COUNTS[k]));
            printLatencyTableRow(rowName, monStats[k].compute());
        }
        printLatencyTableFooter();

        if (calibrated) {
            const double scenarioElapsedSec = static_cast<double>(scenarioElapsedNs) / 1000000000.0;
            const std::uint64_t scenarioObs =
              m_cpuLoad.observedIterPerSec(scenarioIdleDelta, scenarioElapsedNs);
            std::printf("cpu_load: baseline=%llu iter/s (no client traffic)\n",
                        static_cast<unsigned long long>(m_cpuLoad.baselineIterPerSec()));
            std::printf("cpu_load: scalar_u64 read RTT  during=%+.2f%%  "
                        "(observed=%llu iter/s, window=%.2fs)\n",
                        loadDuringScenario * 100.0,
                        static_cast<unsigned long long>(scenarioObs),
                        scenarioElapsedSec);
            std::printf("cpu_load: post-bench           during=%+.2f%%  (window=%u ms, idle reference)\n",
                        loadIdle * 100.0,
                        static_cast<unsigned>(CPU_WINDOW_MS));
            for (std::size_t k = 0; k < MON_COUNTS_N; ++k) {
                if (!monOk[k]) {
                    continue;
                }
                const double winSec = static_cast<double>(monElapsedNs[k]) / 1000000000.0;
                const std::uint64_t obs = m_cpuLoad.observedIterPerSec(monIdleDelta[k], monElapsedNs[k]);
                std::printf("cpu_load: mon_items N=%-3u      during=%+.2f%%  "
                            "(observed=%llu iter/s, window=%.2fs)\n",
                            static_cast<unsigned>(MON_COUNTS[k]),
                            monLoad[k] * 100.0,
                            static_cast<unsigned long long>(obs),
                            winSec);
            }
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
