#include "thread_diagnostics.hpp"

#include <tx_api.h>
#include <tx_thread.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#if defined(HAL_COMPAT_LINUX)
#include "tx_thread_stack_info.hpp"
#endif

namespace thread_diagnostics
{
    namespace
    {
        struct ThreadInfo
        {
            std::uint32_t prio;
            std::string_view name;
            std::size_t stack_size;
            std::size_t current_stack_usage;
            std::size_t peak_stack_usage;
        };

        struct BorderStyle
        {
            std::string_view left;
            std::string_view separator;
            std::string_view right;
        };

        enum class TextAlignment : std::uint8_t
        {
            Left,
            Right,
        };

        constexpr std::size_t BAR_WIDTH{ 30U };
        constexpr std::size_t PERCENT_SCALE{ 100U };
        constexpr std::string_view BORDER_SYMBOL{ "─" };
        constexpr std::string_view PRIO_HEADER{ "Prio" };
        constexpr std::string_view NAME_HEADER{ "Name" };
        constexpr std::string_view AVAIL_HEADER{ "Available [bytes]" };
        constexpr std::string_view CURR_HEADER{ "Current [bytes]" };
        constexpr std::string_view PEAK_HEADER{ "Peak [bytes]" };
        constexpr std::string_view USAGE_HEADER{ "Usage [%]" };
        constexpr std::string_view BAR_HEADER{ "Stack-Usage Bar" };
        constexpr std::string_view NEWLINE{ "\r\n" };
        constexpr BorderStyle TOP_BORDER{ "┌", "┬", "┐" };
        constexpr BorderStyle MIDDLE_BORDER{ "├", "┼", "┤" };
        constexpr BorderStyle BOTTOM_BORDER{ "└", "┴", "┘" };

        void refresh_thread_stack_info(TX_THREAD* thread)
        {
#if defined(TX_ENABLE_STACK_CHECKING) && !defined(HAL_COMPAT_LINUX)
            if (thread == TX_NULL || thread == _tx_thread_current_ptr ||
                thread->tx_thread_stack_ptr == TX_NULL || thread->tx_thread_stack_highest_ptr == TX_NULL) {
                return;
            }

            auto* const current_stack = static_cast<std::byte*>(thread->tx_thread_stack_ptr);
            auto* const highest_stack = static_cast<std::byte*>(thread->tx_thread_stack_highest_ptr);
            if (current_stack < highest_stack) {
                thread->tx_thread_stack_highest_ptr = thread->tx_thread_stack_ptr;
            }
#else
            static_cast<void>(thread);
#endif
        }

        ThreadInfo get_thread_information(TX_THREAD* thread)
        {
            refresh_thread_stack_info(thread);

            auto* const stack_end = static_cast<std::byte*>(thread->tx_thread_stack_end);
            auto* const current_stack = static_cast<std::byte*>(thread->tx_thread_stack_ptr);
            auto* const peak_stack = static_cast<std::byte*>(thread->tx_thread_stack_highest_ptr);
            assert(current_stack <= stack_end && peak_stack <= stack_end);

            return { .prio = thread->tx_thread_priority,
                     .name = thread->tx_thread_name,
                     .stack_size = static_cast<std::size_t>(thread->tx_thread_stack_size),
                     .current_stack_usage = static_cast<std::size_t>(stack_end - current_stack),
                     .peak_stack_usage = static_cast<std::size_t>(stack_end - peak_stack) };
        }

        std::vector<ThreadInfo> get_all_threads_information()
        {
            auto* current_thread{ _tx_thread_created_ptr };
            if (current_thread == TX_NULL || _tx_thread_created_count == 0U) {
                return {};
            }

            std::vector<ThreadInfo> threads_info{};
            threads_info.reserve(_tx_thread_created_count);

            for (ULONG index{}; index < _tx_thread_created_count; ++index) {
                threads_info.push_back(get_thread_information(current_thread));
                current_thread = current_thread->tx_thread_created_next;
                assert(current_thread != TX_NULL);
            }

            return threads_info;
        }

        std::string repeat_string(std::string_view value, std::size_t count)
        {
            std::string result{};
            result.reserve(value.size() * count);
            for (std::size_t index{}; index < count; ++index) {
                result.append(value);
            }
            return result;
        }

        std::string make_border(BorderStyle style, std::size_t name_width)
        {
            const std::array widths{ PRIO_HEADER.size(), name_width,         AVAIL_HEADER.size(),
                                     CURR_HEADER.size(), PEAK_HEADER.size(), USAGE_HEADER.size(),
                                     BAR_WIDTH };

            std::string border{ style.left };
            for (std::size_t index{}; index < widths.size(); ++index) {
                border.append(BORDER_SYMBOL);
                border.append(repeat_string(BORDER_SYMBOL, widths[index]));
                border.append(BORDER_SYMBOL);
                border.append(index + 1U == widths.size() ? style.right : style.separator);
            }
            return border;
        }

        std::string to_text(std::string_view value) { return std::string{ value }; }

        template<std::integral Integer>
        std::string to_text(Integer value)
        {
            return std::to_string(value);
        }

        std::string pad_text(std::string_view text, std::size_t width, TextAlignment alignment)
        {
            if (text.size() >= width) {
                return std::string{ text };
            }

            const auto padding{ width - text.size() };
            if (alignment == TextAlignment::Left) {
                return std::string{ text } + std::string(padding, ' ');
            }
            return std::string(padding, ' ') + std::string{ text };
        }

        std::string format_row(auto prio,
                               std::string_view name,
                               std::size_t name_width,
                               auto available,
                               auto current,
                               auto peak,
                               auto usage,
                               std::string_view bar)
        {
            std::string row{ "│ " };
            row.append(pad_text(to_text(prio), PRIO_HEADER.size(), TextAlignment::Right));
            row.append(" │ ");
            row.append(pad_text(name, name_width, TextAlignment::Left));
            row.append(" │ ");
            row.append(pad_text(to_text(available), AVAIL_HEADER.size(), TextAlignment::Right));
            row.append(" │ ");
            row.append(pad_text(to_text(current), CURR_HEADER.size(), TextAlignment::Right));
            row.append(" │ ");
            row.append(pad_text(to_text(peak), PEAK_HEADER.size(), TextAlignment::Right));
            row.append(" │ ");
            row.append(pad_text(to_text(usage), USAGE_HEADER.size(), TextAlignment::Right));
            row.append(" │ ");
            row.append(pad_text(bar, BAR_WIDTH, TextAlignment::Left));
            row.append(" │");
            return row;
        }

        void print_line(std::string_view text)
        {
            const auto text_written{ std::fwrite(text.data(), sizeof(char), text.size(), stdout) };
            const auto newline_written{ std::fwrite(NEWLINE.data(), sizeof(char), NEWLINE.size(), stdout) };
            assert(text_written == text.size() && newline_written == NEWLINE.size());
            static_cast<void>(text_written);
            static_cast<void>(newline_written);
        }
    } // namespace

    void print_all()
    {
#if defined(HAL_COMPAT_LINUX)
        Tx::Linux::refreshAllThreadsStackInfo();
#endif

        auto threads_info{ get_all_threads_information() };
        if (threads_info.empty()) {
            print_line("No threads running...");
            std::fflush(stdout);
            return;
        }

        std::ranges::sort(threads_info, std::ranges::less{}, &ThreadInfo::prio);

        const auto max_name_width{ std::max(
          NAME_HEADER.size(),
          std::ranges::max(threads_info | std::views::transform([](const ThreadInfo& thread) {
            return thread.name.length();
        }))) };

        const auto top_border{ make_border(TOP_BORDER, max_name_width) };
        const auto middle_border{ make_border(MIDDLE_BORDER, max_name_width) };
        const auto bottom_border{ make_border(BOTTOM_BORDER, max_name_width) };
        const auto header{ format_row(PRIO_HEADER,
                                      NAME_HEADER,
                                      max_name_width,
                                      AVAIL_HEADER,
                                      CURR_HEADER,
                                      PEAK_HEADER,
                                      USAGE_HEADER,
                                      BAR_HEADER) };

        print_line(top_border);
        print_line(header);

        for (const auto& thread : threads_info) {
            const auto current_usage_percent{ (thread.current_stack_usage * PERCENT_SCALE) /
                                              thread.stack_size };
            const auto bar_length{ std::min((current_usage_percent * BAR_WIDTH) / PERCENT_SCALE, BAR_WIDTH) };
            const auto bar{ repeat_string("█", bar_length) + repeat_string("░", BAR_WIDTH - bar_length) };
            const auto info{ format_row(thread.prio,
                                        thread.name,
                                        max_name_width,
                                        thread.stack_size,
                                        thread.current_stack_usage,
                                        thread.peak_stack_usage,
                                        current_usage_percent,
                                        bar) };

            print_line(middle_border);
            print_line(info);
        }

        print_line(bottom_border);
        std::fflush(stdout);
    }
} // namespace thread_diagnostics
