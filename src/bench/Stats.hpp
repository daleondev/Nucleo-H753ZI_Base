#ifndef BENCH_STATS_HPP
#define BENCH_STATS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace bench
{
    /*
     * Fixed-capacity sample buffer for round-trip latency in nanoseconds.
     * Computes count / min / max / mean / stddev / p50 / p95 / p99 over the
     * accumulated samples. Capacity is sized to fit comfortably on the
     * benchmark thread stack on both targets.
     */
    class Stats
    {
      public:
        static constexpr std::size_t CAPACITY{ 1024 };

        struct Result
        {
            std::size_t count;
            std::uint64_t minNs;
            std::uint64_t maxNs;
            double meanNs;
            double stddevNs;
            std::uint64_t p50Ns;
            std::uint64_t p95Ns;
            std::uint64_t p99Ns;
        };

        void add(std::uint64_t sample) noexcept;
        void clear() noexcept { m_size = 0; }
        std::size_t size() const noexcept { return m_size; }
        bool full() const noexcept { return m_size >= CAPACITY; }

        // NOTE: sorts the internal buffer in place. Do not call add() after.
        Result compute();

      private:
        std::array<std::uint64_t, CAPACITY> m_samples{};
        std::size_t m_size{ 0 };
    };
} // namespace bench

#endif // BENCH_STATS_HPP
