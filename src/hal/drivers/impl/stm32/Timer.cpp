#include "Timer.hpp"

#include "hal/drivers/common.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <utility>

namespace hal
{
    namespace
    {
        constexpr std::uint64_t NANOSECONDS_PER_SECOND{ 1'000'000'000ULL };
    }

    Timer::Timer(Configuration configuration)
      : m_handle{ configuration.handle }
      , m_timerInputHz{ configuration.input_frequency_hz }
    {
        if (std::ranges::any_of(s_registry, [&](const Timer* timer) {
                return timer != nullptr && &timer->m_handle == &configuration.handle;
            })) {
            std::terminate();
        }

        auto* const slot{ std::ranges::find(s_registry, nullptr) };
        if (slot == s_registry.end()) {
            std::terminate();
        }
        *slot = this;
    }

    Timer::~Timer()
    {
        const auto slot{ std::ranges::find(s_registry, this) };
        if (slot != s_registry.end()) {
            *slot = nullptr;
        }
    }

    auto Timer::start() noexcept -> util::Result<> { return make_result(HAL_TIM_Base_Start(&m_handle)); }

    auto Timer::stop() noexcept -> util::Result<> { return make_result(HAL_TIM_Base_Stop(&m_handle)); }

    auto Timer::startIt() noexcept -> util::Result<>
    {
        return make_result(HAL_TIM_Base_Start_IT(&m_handle));
    }

    auto Timer::stopIt() noexcept -> util::Result<>
    {
        return make_result(HAL_TIM_Base_Stop_IT(&m_handle));
    }

    auto Timer::getCounter() const noexcept -> Tick
    {
        return static_cast<Tick>(__HAL_TIM_GET_COUNTER(&m_handle));
    }

    auto Timer::setCounter(Tick value) noexcept -> void { __HAL_TIM_SET_COUNTER(&m_handle, value); }

    auto Timer::getAutoReload() const noexcept -> Tick
    {
        return static_cast<Tick>(__HAL_TIM_GET_AUTORELOAD(&m_handle));
    }

    auto Timer::setAutoReload(Tick value) noexcept -> void
    {
        __HAL_TIM_SET_AUTORELOAD(&m_handle, value);
    }

    auto Timer::getPrescaler() const noexcept -> Tick
    {
        return static_cast<Tick>(m_handle.Instance->PSC);
    }

    auto Timer::setPrescaler(Tick value) noexcept -> void { m_handle.Instance->PSC = value; }

    auto Timer::forceUpdateEvent() noexcept -> void { m_handle.Instance->EGR = TIM_EGR_UG; }

    auto Timer::reset() noexcept -> void { setCounter(0U); }

    auto Timer::getInputFrequencyHz() const noexcept -> std::uint32_t { return m_timerInputHz; }

    auto Timer::getTickFrequencyHz() const noexcept -> std::uint32_t
    {
        const std::uint64_t divisor{ static_cast<std::uint64_t>(getPrescaler()) + 1U };
        return static_cast<std::uint32_t>(m_timerInputHz / divisor);
    }

    auto Timer::durationToTicksImpl(std::chrono::nanoseconds duration) const noexcept -> Tick
    {
        const auto nanoseconds{ duration.count() };
        const std::uint64_t frequency{ getTickFrequencyHz() };
        if (nanoseconds <= 0 || frequency == 0U) {
            return 0U;
        }

        const auto unsigned_nanoseconds{ static_cast<std::uint64_t>(nanoseconds) };
        const std::uint64_t seconds{ unsigned_nanoseconds / NANOSECONDS_PER_SECOND };
        const std::uint64_t remainder{ unsigned_nanoseconds % NANOSECONDS_PER_SECOND };
        constexpr std::uint64_t maximum_tick{ std::numeric_limits<Tick>::max() };
        if (seconds > maximum_tick / frequency) {
            return std::numeric_limits<Tick>::max();
        }

        const std::uint64_t whole_ticks{ seconds * frequency };
        const std::uint64_t fractional_ticks{
            (remainder * frequency + NANOSECONDS_PER_SECOND - 1U) / NANOSECONDS_PER_SECOND
        };
        if (fractional_ticks > maximum_tick - whole_ticks) {
            return std::numeric_limits<Tick>::max();
        }
        return static_cast<Tick>(whole_ticks + fractional_ticks);
    }

    auto Timer::setPeriodImpl(std::chrono::nanoseconds duration) noexcept -> void
    {
        const Tick ticks{ durationToTicks(duration) };
        setAutoReload(ticks == 0U ? 0U : ticks - 1U);
        forceUpdateEvent();
    }

    auto Timer::getElapsedTime() const noexcept -> std::chrono::nanoseconds
    {
        const std::uint64_t frequency{ getTickFrequencyHz() };
        if (frequency == 0U) {
            return std::chrono::nanoseconds::zero();
        }

        const std::uint64_t nanoseconds{
            static_cast<std::uint64_t>(getCounter()) * NANOSECONDS_PER_SECOND / frequency
        };
        return std::chrono::nanoseconds{ static_cast<std::int64_t>(nanoseconds) };
    }

    auto Timer::setPeriodElapsedCallbackImpl(PeriodElapsedCallback callback) noexcept -> void
    {
        m_periodElapsedCallback = std::move(callback);
    }

    auto Timer::dispatchPeriodElapsed(TIM_HandleTypeDef* handle) noexcept -> void
    {
        const auto timer{ std::ranges::find_if(s_registry, [&](const Timer* candidate) {
            return candidate != nullptr && &candidate->m_handle == handle;
        }) };
        if (timer != s_registry.end() && (*timer)->m_periodElapsedCallback) {
            (*timer)->m_periodElapsedCallback();
        }
    }
}
