#include "Stats.hpp"

#include <algorithm>
#include <cmath>

namespace bench
{
    void Stats::add(std::uint64_t sample) noexcept
    {
        if (m_size < CAPACITY) {
            m_samples[m_size++] = sample;
        }
    }

    Stats::Result Stats::compute()
    {
        Result r{};
        r.count = m_size;
        if (m_size == 0) {
            return r;
        }

        std::sort(m_samples.begin(), m_samples.begin() + m_size);

        r.minNs = m_samples[0];
        r.maxNs = m_samples[m_size - 1];

        long double sum = 0.0L;
        for (std::size_t i = 0; i < m_size; ++i) {
            sum += static_cast<long double>(m_samples[i]);
        }
        const long double mean = sum / static_cast<long double>(m_size);
        r.meanNs = static_cast<double>(mean);

        long double variance = 0.0L;
        for (std::size_t i = 0; i < m_size; ++i) {
            const long double d = static_cast<long double>(m_samples[i]) - mean;
            variance += d * d;
        }
        variance /= static_cast<long double>(m_size);
        r.stddevNs = static_cast<double>(std::sqrt(static_cast<double>(variance)));

        auto pickPercentile = [&](double p) -> std::uint64_t {
            // nearest-rank percentile, 1-indexed
            std::size_t rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(m_size)));
            if (rank == 0) {
                rank = 1;
            }
            if (rank > m_size) {
                rank = m_size;
            }
            return m_samples[rank - 1];
        };
        r.p50Ns = pickPercentile(0.50);
        r.p95Ns = pickPercentile(0.95);
        r.p99Ns = pickPercentile(0.99);
        return r;
    }
} // namespace bench
