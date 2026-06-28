#include "platform.hpp"

namespace
{
    constexpr uint32_t NANOSECONDS_PER_SECOND{ 1'000'000'000U };

    [[nodiscard]] bool is_leap_year(int32_t year)
    {
        return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    }

    [[nodiscard]] uint32_t days_in_month(int32_t year, uint32_t month)
    {
        constexpr uint8_t days_per_month[]{ 31U, 28U, 31U, 30U, 31U, 30U,
                                            31U, 31U, 30U, 31U, 30U, 31U };
        if (month == 0U || month > 12U) {
            return 0U;
        }
        if (month == 2U && is_leap_year(year)) {
            return 29U;
        }
        return days_per_month[month - 1U];
    }

    [[nodiscard]] int64_t days_from_civil(int32_t year, uint32_t month, uint32_t day)
    {
        year -= month <= 2U ? 1 : 0;
        const int32_t era{ (year >= 0 ? year : year - 399) / 400 };
        const uint32_t year_of_era{ static_cast<uint32_t>(year - era * 400) };
        const uint32_t adjusted_month{ month > 2U ? month - 3U : month + 9U };
        const uint32_t day_of_year{ (153U * adjusted_month + 2U) / 5U + day - 1U };
        const uint32_t day_of_era{
            year_of_era * 365U + year_of_era / 4U - year_of_era / 100U + day_of_year
        };
        return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(day_of_era) - 719468;
    }
}

extern "C" HAL_StatusTypeDef platform_get_system_time(int64_t* seconds_since_epoch,
                                                       uint32_t* nanoseconds)
{
    if (seconds_since_epoch == nullptr || nanoseconds == nullptr) {
        return HAL_ERROR;
    }

    RTC_TimeTypeDef time{};
    RTC_DateTypeDef date{};
    if (HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK ||
        HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }

    /* STM32 stores a 00-99 year and no timezone. Treat the persisted calendar
     * as UTC in the range 2000-2099. */
    const int32_t year{ 2000 + date.Year };
    if (date.Month == 0U || date.Month > 12U || date.Date == 0U ||
        date.Date > days_in_month(year, date.Month) || time.Hours > 23U || time.Minutes > 59U ||
        time.Seconds > 59U || time.SubSeconds > time.SecondFraction) {
        return HAL_ERROR;
    }

    const int64_t days{ days_from_civil(year, date.Month, date.Date) };
    *seconds_since_epoch = days * 86400 + static_cast<int64_t>(time.Hours) * 3600 +
                           static_cast<int64_t>(time.Minutes) * 60 + time.Seconds;
    *nanoseconds = static_cast<uint32_t>(
      static_cast<uint64_t>(time.SecondFraction - time.SubSeconds) * NANOSECONDS_PER_SECOND /
      (static_cast<uint64_t>(time.SecondFraction) + 1U));
    return HAL_OK;
}
