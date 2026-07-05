#include "hal/board/board.hpp"
#include "hal/drivers/factory/ethernet.hpp"
#include "hal/hal.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
    using namespace std::chrono_literals;

    constexpr auto CYCLE_INTERVAL{ 500ms };
    constexpr auto FAILURE_BLINK_INTERVAL{ 100ms };

    static_assert(std::atomic_bool::is_always_lock_free);
    std::atomic_bool user_button_press_pending{};

    auto debug(std::string_view message) -> void
    {
        static_cast<void>(std::fwrite(message.data(), sizeof(char), message.size(), stdout));
        static_cast<void>(std::putchar('\n'));
        static_cast<void>(std::fflush(stdout));
    }

    template<typename... Arguments>
        requires(sizeof...(Arguments) > 0U)
    auto debug(const char* pattern, Arguments... arguments) -> void
    {
        static_cast<void>(std::printf(pattern, arguments...));
        static_cast<void>(std::putchar('\n'));
        static_cast<void>(std::fflush(stdout));
    }

    auto report_user_button_press() -> void
    {
        static const auto yellow_led{ hal::board::createLed(hal::board::LedId::Yellow) };
        if (yellow_led == nullptr) {
            Error_Handler();
        }

        if (user_button_press_pending.exchange(false, std::memory_order_acq_rel)) {
            debug("[input] user button pressed");
            yellow_led->toggle();
        }
    }

    [[nodiscard]] auto run_filex_standard_library_sample() -> bool
    {
#if defined(HAL_PLATFORM_STM32)
        namespace fs = std::filesystem;
        constexpr std::string_view CONTENT{ "FileX through std::fstream\r\n" };
        const fs::path directory{ "/sample" };
        const fs::path file{ directory / "roundtrip.txt" };
        const fs::path copied_file{ directory / "copied.txt" };
        const fs::path renamed_file{ directory / "renamed.txt" };
        std::error_code error;
        static_cast<void>(fs::create_directories(directory / "nested", error));
        if (error) {
            return false;
        }
        std::string input(CONTENT.size(), '\0');
        {
            std::fstream stream{ file, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc };
            stream.write(CONTENT.data(), static_cast<std::streamsize>(CONTENT.size()));
            stream.flush();
            stream.seekg(0);
            stream.read(input.data(), static_cast<std::streamsize>(input.size()));
            if (!stream || stream.gcount() != static_cast<std::streamsize>(CONTENT.size())) {
                return false;
            }
        }
        if (!fs::copy_file(file, copied_file, error) || error) {
            return false;
        }
        fs::resize_file(copied_file, CONTENT.size(), error);
        if (error) {
            return false;
        }
        fs::rename(copied_file, renamed_file, error);
        if (error) {
            return false;
        }
        const fs::space_info volume{ fs::space(directory, error) };
        if (error) {
            return false;
        }
        std::size_t entries{};
        for ([[maybe_unused]] const fs::directory_entry& entry :
             fs::recursive_directory_iterator{ directory, error }) {
            ++entries;
        }
        const bool valid{ !error && entries == 3U && input == CONTENT &&
                          fs::file_size(file, error) == CONTENT.size() &&
                          fs::file_size(renamed_file, error) == CONTENT.size() &&
                          fs::current_path(error) == "/" && volume.capacity == 32U * 1024U &&
                          volume.available <= volume.capacity };
        const std::uintmax_t removed{ fs::remove_all(directory, error) };
        return valid && !error && removed == 4U && !fs::exists(directory, error) && !error;
#else
        return true;
#endif
    }

    [[noreturn]] auto indicate_failure() -> void
    {
        const auto red_led{ hal::board::createLed(hal::board::LedId::Red) };
        if (red_led == nullptr) {
            Error_Handler();
        }
        debug("[ethercat] TEST FAILED - red LED indicates failure");
        while (true) {
            report_user_button_press();
            red_led->toggle();
            std::this_thread::sleep_for(FAILURE_BLINK_INTERVAL);
        }
    }
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
    if (!run_filex_standard_library_sample()) {
        debug("[filex] std::fstream/std::filesystem sample failed");
        indicate_failure();
    }
    debug("[filex] std::fstream/std::filesystem sample passed");

    const auto user_button{ hal::board::createButton(hal::board::ButtonId::User) };
    if (user_button == nullptr) {
        debug("[input] user button creation failed");
        indicate_failure();
    }
    user_button->setStateChangedCallback([](hal::device::IButton::State state) noexcept {
        if (state == hal::device::IButton::State::Pressed) {
            user_button_press_pending.store(true, std::memory_order_release);
        }
    });

    const auto green_led{ hal::board::createLed(hal::board::LedId::Green) };
    if (green_led == nullptr) {
        debug("[input] user button creation failed");
        indicate_failure();
    }

    while (true) {
        report_user_button_press();
        green_led->toggle();
        std::this_thread::sleep_for(CYCLE_INTERVAL);
    }
    indicate_failure();
}
