#pragma once

#include "hal/drivers/itf/ITimer.hpp"

#include <map>

namespace hal
{
    class Timer final : public ITimer
    {
      public:
        Timer(TIM_HandleTypeDef& handle, std::uint32_t timerInputHz);
        ~Timer();

        [[nodiscard]] auto start() noexcept -> util::Result override;
        [[nodiscard]] auto stop() noexcept -> util::Result override;

        [[nodiscard]] auto startIt() noexcept -> util::Result override;
        [[nodiscard]] auto stopIt() noexcept -> util::Result override;

        [[nodiscard]] auto getCounter() const noexcept -> Tick override;
        auto setCounter(Tick value) noexcept -> void override;

        [[nodiscard]] auto getAutoReload() const noexcept -> Tick override;
        auto setAutoReload(Tick value) noexcept -> void override;

        [[nodiscard]] auto getPrescaler() const noexcept -> Tick override;
        auto setPrescaler(Tick value) noexcept -> void override;

        auto forceUpdateEvent() noexcept -> void override;
        auto reset() noexcept -> void override;

        [[nodiscard]] auto getInputFrequencyHz() const noexcept -> std::uint32_t override;
        [[nodiscard]] auto getTickFrequencyHz() const noexcept -> std::uint32_t override;

        [[nodiscard]] auto getElapsedTime() const noexcept -> std::chrono::nanoseconds override;

      private:
        [[nodiscard]] auto durationToTicksImpl(std::chrono::nanoseconds duration) const noexcept
          -> Tick override;

        auto setPeriodImpl(std::chrono::nanoseconds duration) noexcept -> void override;

        auto setPeriodElapsedCallbackImpl(IsrCallback callback) noexcept -> void override;

        static auto dispatchPeriodElapsed(TIM_HandleTypeDef* handle) -> void noexcept;

        TIM_HandleTypeDef& m_handle;
        std::uint32_t m_timerInputHz;
        IsrCallback m_periodElapsedCallback{ nullptr };

        inline static std::map<TIM_HandleTypeDef*, Timer*> s_registry{};
    };
}
