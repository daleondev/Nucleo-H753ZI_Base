#pragma once

#include <functional>

namespace hal::device
{
    enum class ButtonState
    {
        Released,
        Pressed
    };

    class IButton
    {
      public:
        using StateChangedCallback = std::move_only_function<void(ButtonState) noexcept>;

        virtual ~IButton() = default;

        IButton(const IButton&) = delete;
        IButton& operator=(const IButton&) = delete;
        IButton(IButton&&) = delete;
        IButton& operator=(IButton&&) = delete;

        [[nodiscard]] virtual auto state() const noexcept -> ButtonState = 0;
        [[nodiscard]] auto isPressed() const noexcept -> bool { return state() == ButtonState::Pressed; }

        // The callback inherits the execution context of the underlying input.
        virtual auto setStateChangedCallback(StateChangedCallback callback) noexcept -> void = 0;
        auto clearStateChangedCallback() noexcept -> void { setStateChangedCallback({}); }

      protected:
        IButton() = default;
    };
}
