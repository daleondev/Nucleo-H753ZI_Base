#include "Timer.hpp"

#include "hal/drivers/common.hpp"

namespace hal
{
    Timer::Timer(TIM_HandleTypeDef& handle, std::uint32_t timerInputHz)
      : m_handle{ handle }
      , m_timerInputHz{ timerInputHz }
    {
        s_registry.emplace(&handle, this);
    }

    Timer::~Timer() { s_registry.erase(m_handle); }

    auto Timer::start() noexcept -> util::Result { return make_result(HAL_TIM_Base_Start(handle_)); }

    auto Timer::stop() noexcept -> util::Result { return make_result(HAL_TIM_Base_Stop(handle_)); }

    auto Timer::startIt() noexcept -> util::Result { return make_result(HAL_TIM_Base_Start_IT(handle_)); }
    auto Timer::stopIt() noexcept -> util::Result { return make_result(HAL_TIM_Base_Stop_IT(handle_)); }

    auto Timer::getCounter() const noexcept -> Tick
    {
        return static_cast<Tick>(__HAL_TIM_GET_COUNTER(handle_));
    }
    auto Timer::setCounter(Tick value) noexcept -> void { __HAL_TIM_SET_COUNTER(handle_, value); }

    auto Timer::getAutoReload() const noexcept -> Tick
    {
        return static_cast<Tick>(__HAL_TIM_GET_AUTORELOAD(handle_));
    }
    auto Timer::setAutoReload(Tick value) noexcept -> void { __HAL_TIM_SET_AUTORELOAD(handle_, value); }

    auto Timer::getPrescaler() const noexcept -> Tick { return static_cast<Tick>(handle_->Instance->PSC); }
    auto Timer::setPrescaler(Tick value) noexcept -> void { handle_->Instance->PSC = value; }

    auto Timer::forceUpdateEvent() noexcept -> void { handle_->Instance->EGR = TIM_EGR_UG; }
    auto Timer::reset() noexcept -> void { setCounter(0); }

    auto Timer::getInputFrequencyHz() const noexcept -> std::uint32_t { return m_timerInputHz; }
    auto Timer::getTickFrequencyHz() const noexcept -> std::uint32_t
    {
        return m_timerInputHz / (getPrescaler() + 1U);
    }

    auto Timer::durationToTicksImpl(std::chrono::nanoseconds duration) const noexcept -> Tick
    {
        const auto ns{ duration.count() };
        if (ns <= 0) {
            return 0;
        }

        const auto hz{ static_cast<std::uint64_t>(getTickFrequencyHz()) };
        const auto ticks{ (static_cast<std::uint64_t>(ns) * hz + 999'999'999ULL) / 1'000'000'000ULL };

        if (ticks > 0xFFFF'FFFFULL) {
            return 0xFFFF'FFFFUL;
        }

        return static_cast<Tick>(ticks);
    }

    auto Timer::setPeriodImpl(std::chrono::nanoseconds duration) noexcept -> void
    {
        const auto ticks{ durationToTicks(duration) };
        setAutoReload(ticks == 0 ? 0 : ticks - 1U);
        forceUpdateEvent();
    }

    auto Timer::getElapsedTime() const noexcept -> std::chrono::nanoseconds
    {
        const auto ticks{ static_cast<std::uint64_t>(getCounter()) };
        const auto hz{ static_cast<std::uint64_t>(getTickFrequencyHz()) };

        if (hz == 0) {
            return std::chrono::nanoseconds{ 0 };
        }

        const auto ns{ (ticks * 1'000'000'000ULL) / hz };
        return std::chrono::nanoseconds{ ns };
    }

    auto Timer::setPeriodElapsedCallbackImpl(IsrCallback callback) noexcept -> void
    {
        m_periodElapsedCallback = callback;
    }

    auto Timer::dispatchPeriodElapsed(TIM_HandleTypeDef* handle) -> void noexcept
    {
        if (handle && s_registry.contains(handle)) {
            std::invoke(s_registry[handle]->m_periodElapsedCallback);
        }
    }
}