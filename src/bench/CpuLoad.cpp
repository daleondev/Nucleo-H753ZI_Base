#include "CpuLoad.hpp"

namespace bench
{
    namespace
    {
        constexpr UINT IDLE_THREAD_PRIO{ 30 };
        constexpr ULONG RELINQUISH_INTERVAL{ 1024 };

        // ThreadX passes a single ULONG (32-bit on x86_64-linux), too small
        // for a pointer. The application instantiates a single CpuLoad, so
        // we stash it in a file-scope pointer.
        CpuLoad* s_instance{ nullptr };

        ULONG msToTicks(std::uint32_t ms)
        {
            const ULONG ticks = (static_cast<ULONG>(ms) * TX_TIMER_TICKS_PER_SECOND + 999UL) / 1000UL;
            return ticks == 0 ? 1UL : ticks;
        }
    } // namespace

    bool CpuLoad::start()
    {
        if (s_instance != nullptr) {
            return false;
        }
        m_running.store(true, std::memory_order_release);
        s_instance = this;
        const UINT status = tx_thread_create(&m_thread,
                                             const_cast<CHAR*>("bench_idle"),
                                             CpuLoad::txEntry,
                                             0UL,
                                             m_stack.data(),
                                             static_cast<ULONG>(m_stack.size()),
                                             IDLE_THREAD_PRIO,
                                             IDLE_THREAD_PRIO,
                                             TX_NO_TIME_SLICE,
                                             TX_AUTO_START);
        if (status != TX_SUCCESS) {
            m_running.store(false, std::memory_order_release);
            s_instance = nullptr;
            return false;
        }
        return true;
    }

    void CpuLoad::stop()
    {
        if (!m_running.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        // Allow the loop to observe the flag and exit.
        tx_thread_sleep(static_cast<ULONG>(2));
        tx_thread_terminate(&m_thread);
        tx_thread_delete(&m_thread);
        s_instance = nullptr;
    }

    bool CpuLoad::calibrate(std::uint32_t windowMs)
    {
        if (windowMs == 0) {
            return false;
        }
        // Take several samples and keep the *highest* observed rate as the
        // baseline. Startup transients (DHCP, server init, ARP, lwIP timers)
        // can briefly suppress idle for hundreds of ms; if we used a single
        // sample the baseline could be artificially low and the subsequent
        // scenario would then appear to have *negative* load.
        constexpr std::uint32_t SAMPLES = 4;
        std::uint64_t maxRate = 0;
        for (std::uint32_t i = 0; i < SAMPLES; ++i) {
            const std::uint32_t before = snapshotCounter();
            tx_thread_sleep(msToTicks(windowMs));
            const std::uint32_t after = snapshotCounter();
            const std::uint32_t delta = static_cast<std::uint32_t>(after - before);
            const std::uint64_t rate =
              (static_cast<std::uint64_t>(delta) * 1000ULL) / static_cast<std::uint64_t>(windowMs);
            if (rate > maxRate) {
                maxRate = rate;
            }
        }
        if (maxRate == 0) {
            return false;
        }
        m_baselineIterPerSec = maxRate;
        return true;
    }

    double CpuLoad::sample(std::uint32_t windowMs)
    {
        if (m_baselineIterPerSec == 0 || windowMs == 0) {
            return 0.0;
        }
        const std::uint32_t before = snapshotCounter();
        tx_thread_sleep(msToTicks(windowMs));
        const std::uint32_t after = snapshotCounter();
        const std::uint32_t delta = static_cast<std::uint32_t>(after - before);
        const std::uint64_t observedIterPerSec =
          (static_cast<std::uint64_t>(delta) * 1000ULL) / static_cast<std::uint64_t>(windowMs);
        // Do not clamp at observed >= baseline. Tiny negative loads are a
        // useful indicator of measurement noise; clamping them to 0 hides
        // the noise floor and makes a real near-zero load indistinguishable
        // from a misconfigured baseline.
        const double load =
          1.0 - (static_cast<double>(observedIterPerSec) / static_cast<double>(m_baselineIterPerSec));
        if (load > 1.0) {
            return 1.0;
        }
        return load;
    }

    double CpuLoad::loadOver(std::uint32_t deltaIter, std::uint64_t elapsedNs) const noexcept
    {
        if (m_baselineIterPerSec == 0 || elapsedNs == 0) {
            return 0.0;
        }
        // observed iter/s = deltaIter * 1e9 / elapsedNs
        const std::uint64_t observedIterPerSec =
          (static_cast<std::uint64_t>(deltaIter) * 1000000000ULL) / elapsedNs;
        const double load =
          1.0 - (static_cast<double>(observedIterPerSec) / static_cast<double>(m_baselineIterPerSec));
        if (load > 1.0) {
            return 1.0;
        }
        return load;
    }

    std::uint64_t CpuLoad::observedIterPerSec(std::uint32_t deltaIter, std::uint64_t elapsedNs) const noexcept
    {
        if (elapsedNs == 0) {
            return 0;
        }
        return (static_cast<std::uint64_t>(deltaIter) * 1000000000ULL) / elapsedNs;
    }

    void CpuLoad::txEntry(ULONG argument)
    {
        (void)argument;
        if (s_instance != nullptr) {
            s_instance->run();
        }
    }

    void CpuLoad::run()
    {
        ULONG inner = 0;
        while (m_running.load(std::memory_order_acquire)) {
            m_counter.fetch_add(1, std::memory_order_relaxed);
            if (++inner >= RELINQUISH_INTERVAL) {
                inner = 0;
                tx_thread_relinquish();
            }
        }
    }
} // namespace bench
