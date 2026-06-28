#pragma once

#include <expected>
#include <system_error>

namespace util
{
    template<typename T = void>
    using Result = std::expected<T, std::error_code>;

    namespace result
    {
        template<typename T>
            requires(std::is_void_v<T>)
        inline constexpr auto success() noexcept -> Result<T>
        {
            return {};
        }

        template<typename T>
            requires(!std::is_void_v<T>)
        inline constexpr auto success(const T& value) noexcept -> Result<T>
        {
            return value;
        }

        template<typename T>
            requires(!std::is_void_v<T>)
        inline constexpr auto fail(std::error_code err) noexcept -> Result<T>
        {
            return std::unexpected(std::move(err));
        }
    }
}