#include "hal/drivers/factory/rtc.hpp"
#include "hal/drivers/factory/timer.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

using namespace std::chrono_literals;

namespace
{
    [[nodiscard]] auto total_nanoseconds(const hal::IRtc::Timestamp& timestamp) -> std::int64_t
    {
        return (timestamp.seconds_since_epoch * hal::IRtc::NANOSECONDS_PER_SECOND) +
               timestamp.nanoseconds;
    }
}

TEST(HalClockDrivers, RtcProvidesRealtime)
{
    const auto rtc{ hal::rtc::create() };
    ASSERT_NE(rtc, nullptr);

    const auto before{ rtc->getTime() };
    ASSERT_TRUE(before.has_value());
    EXPECT_LT(before->nanoseconds, hal::IRtc::NANOSECONDS_PER_SECOND);

    std::this_thread::sleep_for(2ms);

    const auto system_time{ std::chrono::system_clock::now().time_since_epoch() };
    const auto system_nanoseconds{
        std::chrono::duration_cast<std::chrono::nanoseconds>(system_time).count()
    };

    const auto after{ rtc->getTime() };
    ASSERT_TRUE(after.has_value());

    EXPECT_GE(system_nanoseconds, total_nanoseconds(*before));
    EXPECT_LE(system_nanoseconds, total_nanoseconds(*after));
    EXPECT_GT(total_nanoseconds(*after), total_nanoseconds(*before));
}

TEST(HalClockDrivers, TimerProvidesHighResolutionCounter)
{
    constexpr std::size_t high_resolution_timer_index{ 2U };
    const auto timer{ hal::timer::create(high_resolution_timer_index) };
    ASSERT_NE(timer, nullptr);
    ASSERT_GT(timer->getTickFrequencyHz(), 0U);

    const std::uint64_t modulus{ static_cast<std::uint64_t>(timer->getAutoReload()) + 1U };
    const auto before{ timer->getCounter() };
    std::this_thread::sleep_for(2ms);
    const auto after{ timer->getCounter() };
    const std::uint64_t elapsed{ after >= before ? after - before : modulus - before + after };
    EXPECT_GT(elapsed, 0U);
}
