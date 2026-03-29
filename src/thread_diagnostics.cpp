#include "thread_diagnostics.hpp"

#include <tx_api.h>
#include <tx_thread.h>

#include <cassert>
#include <cstdint>
#include <cstdio>

#include <algorithm>
#include <format>
#include <ranges>
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
            uint32_t prio;
            std::string_view name;
            size_t stack_size;
            size_t current_stack_usage;
            size_t peak_stack_usage;
        };

        ThreadInfo getThreadInformation(TX_THREAD* thread)
        {
            const auto stack_end = reinterpret_cast<uintptr_t>(thread->tx_thread_stack_end);
            const auto current_stack_ptr = reinterpret_cast<uintptr_t>(thread->tx_thread_stack_ptr);
            const auto peak_stack_ptr = reinterpret_cast<uintptr_t>(thread->tx_thread_stack_highest_ptr);
            assert(current_stack_ptr <= stack_end && peak_stack_ptr <= stack_end);

            return { .prio = thread->tx_thread_priority,
                     .name = thread->tx_thread_name,
                     .stack_size = static_cast<size_t>(thread->tx_thread_stack_size),
                     .current_stack_usage = static_cast<size_t>(stack_end - current_stack_ptr),
                     .peak_stack_usage = static_cast<size_t>(stack_end - peak_stack_ptr) };
        }

        std::vector<ThreadInfo> getAllThreadsInformation()
        {
            auto* first_thread{ _tx_thread_created_ptr };
            if (first_thread == TX_NULL || _tx_thread_created_count == 0U) {
                std::printf("No threads created\n");
                return {};
            }

            std::vector<ThreadInfo> threads_info{};
            threads_info.reserve(_tx_thread_created_count);

            auto* current_thread{ first_thread };
            do {
                threads_info.push_back(getThreadInformation(current_thread));
                current_thread = current_thread->tx_thread_created_next;
            } while (current_thread != first_thread);

            return threads_info;
        }

        template<size_t Size>
        struct FixedString
        {
            constexpr explicit(false) FixedString(const char* str) { std::copy_n(str, Size, data.begin()); }
            constexpr auto operator<=>(const FixedString&) const = default;
            constexpr explicit(false) operator std::string_view() const { return { data.data(), Size }; }
            constexpr auto size() const { return Size; }
            std::array<char, Size + 1uz> data{};
        };
        template<class T, size_t Capacity, size_t Size = Capacity - 1>
        FixedString(const T (&str)[Capacity]) -> FixedString<Size>;

        constexpr std::string repeatStr(std::string_view sv, size_t n)
        {
            return std::views::repeat(sv, n) | std::views::join | std::ranges::to<std::string>();
        }

        template<const std::string_view& SV, size_t N>
        consteval FixedString<N * SV.length()> repeatStr()
        {
            return FixedString<N * SV.length()>(repeatStr(SV, N).data());
        }

        using namespace std::literals;

        static constexpr size_t BAR_WIDTH{ 30 };
        static constexpr auto BORDER_SYMBOL{ "─"sv };

        static constexpr FixedString TOP_BORDER_FMT{ "┌─{}─┬─{}─┬─{}─┬─{}─┬─{}─┬─{}─┬─{}─┐" };
        static constexpr FixedString MID_BORDER_FMT{ "├─{}─┼─{}─┼─{}─┼─{}─┼─{}─┼─{}─┼─{}─┤" };
        static constexpr FixedString BOT_BORDER_FMT{ "└─{}─┴─{}─┴─{}─┴─{}─┴─{}─┴─{}─┴─{}─┘" };

        static constexpr auto PRIO_HEADER{ "Prio"sv };
        static constexpr auto NAME_HEADER{ "Name"sv };
        static constexpr auto AVAIL_HEADER{ "Available [bytes]"sv };
        static constexpr auto CURR_HEADER{ "Current [bytes]"sv };
        static constexpr auto PEAK_HEADER{ "Peak [bytes]"sv };
        static constexpr auto USAGE_HEADER{ "Usage [%]"sv };
        static constexpr auto BAR_HEADER{ "Stack-Usage Bar"sv };

        static constexpr auto PRIO_BORDER{ repeatStr<BORDER_SYMBOL, PRIO_HEADER.length()>() };
        static constexpr auto AVAIL_BORDER{ repeatStr<BORDER_SYMBOL, AVAIL_HEADER.length()>() };
        static constexpr auto CURR_BORDER{ repeatStr<BORDER_SYMBOL, CURR_HEADER.length()>() };
        static constexpr auto PEAK_BORDER{ repeatStr<BORDER_SYMBOL, PEAK_HEADER.length()>() };
        static constexpr auto USAGE_BORDER{ repeatStr<BORDER_SYMBOL, USAGE_HEADER.length()>() };
        static constexpr auto BAR_BORDER{ repeatStr<BORDER_SYMBOL, BAR_WIDTH>() };

        template<FixedString Fmt>
        std::string makeBorder(std::string_view name_border)
        {
            return std::format(Fmt,
                               static_cast<std::string_view>(PRIO_BORDER),
                               name_border,
                               static_cast<std::string_view>(AVAIL_BORDER),
                               static_cast<std::string_view>(CURR_BORDER),
                               static_cast<std::string_view>(PEAK_BORDER),
                               static_cast<std::string_view>(USAGE_BORDER),
                               static_cast<std::string_view>(BAR_BORDER));
        }

        // clang-format off
        std::string formatRow(auto prio, std::string_view name, size_t name_len, auto avail, auto curr, auto peak, auto usage, std::string_view bar) {
            return std::format("│ {:>{}} │ {:<{}} │ {:>{}} │ {:>{}} │ {:>{}} │ {:>{}} │ {:<{}} │",
                prio,  PRIO_HEADER.length(),
                name,  name_len,
                avail, AVAIL_HEADER.length(),
                curr,  CURR_HEADER.length(),
                peak,  PEAK_HEADER.length(),
                usage, USAGE_HEADER.length(),
                bar,   BAR_WIDTH
            );
        };
        // clang-format on
    } // namespace

    void printAll()
    {
#if defined(HAL_COMPAT_LINUX)
        Tx::Linux::refreshAllThreadsStackInfo();
#endif

        auto threads_info{ getAllThreadsInformation() };
        if (threads_info.empty()) {
            std::printf("No threads running...\r\n");
            std::fflush(stdout);
            return;
        }

        std::ranges::sort(threads_info, std::ranges::less{}, &ThreadInfo::prio);

        auto max_name_len{ std::max(
          NAME_HEADER.size(), std::ranges::max(threads_info | std::views::transform([](const ThreadInfo& s) {
            return s.name.length();
        }))) };
        auto name_border{ repeatStr(BORDER_SYMBOL, max_name_len) };

        auto top_border{ makeBorder<TOP_BORDER_FMT>(name_border) };
        auto mid_border{ makeBorder<MID_BORDER_FMT>(name_border) };
        auto bot_border{ makeBorder<BOT_BORDER_FMT>(name_border) };

        auto header{ formatRow(PRIO_HEADER,
                               NAME_HEADER,
                               max_name_len,
                               AVAIL_HEADER,
                               CURR_HEADER,
                               PEAK_HEADER,
                               USAGE_HEADER,
                               BAR_HEADER) };

        std::printf("%s\r\n", top_border.c_str());
        std::printf("%s\r\n", header.c_str());

        // thread infos
        for (const auto& thread : threads_info) {
            auto current_usage_percent{ (thread.current_stack_usage * 100) / thread.stack_size };
            auto bar_length = std::min<size_t>((current_usage_percent * BAR_WIDTH) / 100, BAR_WIDTH);

            auto info{ formatRow(thread.prio,
                                 thread.name,
                                 max_name_len,
                                 thread.stack_size,
                                 thread.current_stack_usage,
                                 thread.peak_stack_usage,
                                 current_usage_percent,
                                 repeatStr("█"sv, bar_length) + repeatStr("░"sv, BAR_WIDTH - bar_length)) };

            std::printf("%s\r\n", mid_border.c_str());
            std::printf("%s\r\n", info.c_str());
        }

        std::printf("%s\r\n", bot_border.c_str());
        std::fflush(stdout);
    }
} // namespace thread_diagnostics