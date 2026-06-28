#pragma once

#include "utilities/Result.hpp"

#include <chrono>
#include <concepts>
#include <cstdint>
#include <functional>

namespace hal
{

    namespace detail
    {
        template<typename Func, typename... Args>
        concept NothrowPeriodElapsedCallback =
          std::is_nothrow_constructible_v<std::remove_cvref_t<Func>, Func&&> &&
          (std::is_nothrow_constructible_v<std::remove_cvref_t<Args>, Args&&> && ...) &&
          std::is_nothrow_invocable_v<std::remove_cvref_t<Func>&, std::remove_cvref_t<Args>&...>;
    }

    class ITimer
    {
      public:
        using Tick = std::uint32_t;

        virtual ~ITimer() = default;

        [[nodiscard]] virtual auto start() noexcept -> util::Result = 0;
        [[nodiscard]] virtual auto stop() noexcept -> util::Result = 0;

        [[nodiscard]] virtual auto startIt() noexcept -> util::Result = 0;
        [[nodiscard]] virtual auto stopIt() noexcept -> util::Result = 0;

        [[nodiscard]] virtual auto getCounter() const noexcept -> Tick = 0;
        virtual auto setCounter(Tick value) noexcept -> void = 0;

        [[nodiscard]] virtual auto getAutoReload() const noexcept -> Tick = 0;
        virtual auto setAutoReload(Tick value) noexcept -> void = 0;

        [[nodiscard]] virtual auto getPrescaler() const noexcept -> Tick = 0;
        virtual auto setPrescaler(Tick value) noexcept -> void = 0;

        virtual auto forceUpdateEvent() noexcept -> void = 0;
        virtual auto reset() noexcept -> void = 0;

        [[nodiscard]] virtual auto getInputFrequencyHz() const noexcept -> std::uint32_t = 0;
        [[nodiscard]] virtual auto getTickFrequencyHz() const noexcept -> std::uint32_t = 0;

        template<typename Rep, typename Period>
        [[nodiscard]] auto durationToTicks(std::chrono::duration<Rep, Period> duration) const noexcept -> Tick
        {
            return durationToTicksImpl(std::chrono::duration_cast<std::chrono::nanoseconds>(duration));
        }

        template<typename Rep, typename Period>
        auto setPeriod(std::chrono::nanoseconds duration) noexcept -> void
        {
            setPeriodImpl(std::chrono::duration_cast<std::chrono::nanoseconds>(duration));
        }

        [[nodiscard]] virtual auto getElapsedTime() const noexcept -> std::chrono::nanoseconds = 0;

        template<typename Func, typename... Args>
            requires detail::NothrowPeriodElapsedCallback<Func, Args...>
        auto setPeriodElapsedCallback(Func&& callback, Args&&... args) const noexcept -> void
        {
            setPeriodElapsedCallbackImpl(
              [cb = std::forward<Func>(callback),
               ... bound_args = std::forward<Args>(args)]() mutable noexcept -> void {
                std::invoke(cb, bound_args...);
            });
        }

      protected:
        [[nodiscard]] virtual auto durationToTicksImpl(std::chrono::nanoseconds duration) const noexcept
          -> Tick = 0;

        virtual auto setPeriodImpl(std::chrono::nanoseconds duration) noexcept -> void = 0;

        using IsrCallback = void (*)() noexcept;
        virtual auto setPeriodElapsedCallbackImpl(IsrCallback callback) noexcept -> void = 0;
    };
}
