#include "ServerCpuLoadReporter.hpp"

#include "CpuLoad.hpp"

#include <cstdio>

namespace bench
{
    namespace
    {
        constexpr UINT REPORTER_THREAD_PRIO{ 16 };
        constexpr ULONG REPORT_PERIOD_MS{ 1000 };

        ServerCpuLoadReporter* s_instance{ nullptr };

        ULONG msToTicks(ULONG ms)
        {
            const ULONG ticks = (ms * TX_TIMER_TICKS_PER_SECOND + 999UL) / 1000UL;
            return ticks == 0 ? 1UL : ticks;
        }
    } // namespace

    ServerCpuLoadReporter::~ServerCpuLoadReporter() { stop(); }

    bool ServerCpuLoadReporter::start()
    {
        if (m_running.load(std::memory_order_acquire) || s_instance != nullptr) {
            return false;
        }
        m_running.store(true, std::memory_order_release);
        s_instance = this;
        const UINT status = tx_thread_create(&m_thread,
                                             const_cast<CHAR*>("srv_cpu_rep"),
                                             ServerCpuLoadReporter::txEntry,
                                             0UL,
                                             m_stack.data(),
                                             static_cast<ULONG>(m_stack.size()),
                                             REPORTER_THREAD_PRIO,
                                             REPORTER_THREAD_PRIO,
                                             TX_NO_TIME_SLICE,
                                             TX_AUTO_START);
        if (status != TX_SUCCESS) {
            m_running.store(false, std::memory_order_release);
            s_instance = nullptr;
            return false;
        }
        return true;
    }

    void ServerCpuLoadReporter::stop()
    {
        if (!m_running.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        tx_thread_sleep(2);
        tx_thread_terminate(&m_thread);
        tx_thread_delete(&m_thread);
        s_instance = nullptr;
    }

    void ServerCpuLoadReporter::txEntry(ULONG argument)
    {
        (void)argument;
        if (s_instance != nullptr) {
            s_instance->run();
        }
    }

    void ServerCpuLoadReporter::run()
    {
        CpuLoad& cpu = globalCpuLoad();
        const ULONG period = msToTicks(REPORT_PERIOD_MS);
        // Use the local ms-since-start as the timestamp domain (matches
        // the existing bench output's t=<uptime_ms> style). Computed as
        // the cumulative period count rather than reading a system tick,
        // since we don't have a portable wall-clock helper here.
        std::uint32_t cumulativeMs = 0;

        std::uint32_t prevCounter = cpu.counter();
        while (m_running.load(std::memory_order_acquire)) {
            tx_thread_sleep(period);
            cumulativeMs += REPORT_PERIOD_MS;
            const std::uint32_t now = cpu.counter();
            const std::uint32_t delta = static_cast<std::uint32_t>(now - prevCounter);
            prevCounter = now;
            const std::uint64_t windowNs = static_cast<std::uint64_t>(REPORT_PERIOD_MS) * 1000000ULL;
            const std::uint64_t observed = cpu.observedIterPerSec(delta, windowNs);
            const double loadPct = cpu.loadOver(delta, windowNs) * 100.0;
            const std::uint64_t baseline = cpu.baselineIterPerSec();
            std::printf("srv_cpu_load: t=%u  load=%+6.2f%%  observed=%llu iter/s  "
                        "baseline=%llu iter/s\n",
                        static_cast<unsigned>(cumulativeMs),
                        loadPct,
                        static_cast<unsigned long long>(observed),
                        static_cast<unsigned long long>(baseline));
            std::fflush(stdout);
        }
    }
} // namespace bench
