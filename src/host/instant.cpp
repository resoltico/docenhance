// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "instant.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace docenhance::host {
namespace {
constexpr std::int64_t seconds_per_minute = 60;
constexpr std::int64_t seconds_per_hour = 60 * seconds_per_minute;
constexpr std::int64_t seconds_per_day = 24 * seconds_per_hour;
constexpr std::uint32_t decimal = 10;

// Howard Hinnant's days-to-civil algorithm, over an era of 400 years beginning on 1 March 0000.
// Starting a year in March puts the leap day at its end, so day-of-year is one arithmetic
// sequence and no month needs a table.
constexpr std::int64_t days_per_era = 146097;
constexpr std::int64_t years_per_era = 400;
constexpr std::int64_t epoch_day_of_era = 719468; // 1970-01-01 within the era that contains it.
constexpr std::uint32_t days_per_common_year = 365;
constexpr std::uint32_t days_per_leap_cycle = 1460;   // Four common years.
constexpr std::uint32_t days_per_century = 36524;     // A century whose last year is common.
constexpr std::uint32_t days_per_long_cycle = 146096; // The era, less its final leap day.
constexpr std::uint32_t years_per_leap = 4;
constexpr std::uint32_t years_per_century = 100;
constexpr std::uint32_t days_per_month_cycle = 153; // Five months, in the March-first ordering.
constexpr std::uint32_t month_numerator = 5;
constexpr std::uint32_t month_offset = 2;
constexpr std::uint32_t months_before_january = 10; // Positions 10 and 11 are January, February.
constexpr std::uint32_t march = 3;
constexpr std::uint32_t january_shift = 9;
constexpr std::size_t year_digits = 4;
constexpr std::size_t field_digits = 2;

struct CivilDate {
    std::int64_t year = 0;
    std::uint32_t month = 0;
    std::uint32_t day = 0;
};

CivilDate civil_from_days(std::int64_t day) noexcept {
    const std::int64_t shifted = day + epoch_day_of_era;
    const std::int64_t era = (shifted >= 0 ? shifted : shifted - (days_per_era - 1)) / days_per_era;
    const auto day_of_era = static_cast<std::uint32_t>(shifted - (era * days_per_era));
    const std::uint32_t year_of_era =
        ((day_of_era - (day_of_era / days_per_leap_cycle)) + (day_of_era / days_per_century) -
         (day_of_era / days_per_long_cycle)) /
        days_per_common_year;
    const std::uint32_t day_of_year =
        day_of_era - ((days_per_common_year * year_of_era) + (year_of_era / years_per_leap) -
                      (year_of_era / years_per_century));
    const std::uint32_t position =
        ((month_numerator * day_of_year) + month_offset) / days_per_month_cycle;
    const std::uint32_t day_of_month =
        (day_of_year - (((days_per_month_cycle * position) + month_offset) / month_numerator)) + 1;
    // Both branches stay within the unsigned range, which is the point of writing this out.
    const bool before_january = position < months_before_january;
    const std::uint32_t month = before_january ? position + march : position - january_shift;
    const std::int64_t year =
        static_cast<std::int64_t>(year_of_era) + (era * years_per_era) + (before_january ? 0 : 1);
    return {.year = year, .month = month, .day = day_of_month};
}

// The value right-aligned in at least this many digits, zero-padded, and longer when it does not
// fit. A negative year is written with a leading minus, which no clock this reads can produce.
void append_padded(std::string& text, std::int64_t value, std::size_t digits) {
    if (value < 0) {
        text.push_back('-');
    }
    auto magnitude = static_cast<std::uint64_t>(value < 0 ? -value : value);
    std::size_t width = 1;
    for (auto remaining = magnitude / decimal; remaining > 0; remaining /= decimal) {
        ++width;
    }
    const auto start = text.size();
    text.append(width > digits ? width : digits, '0');
    for (auto position = text.size(); position > start && magnitude > 0; --position) {
        text.at(position - 1) = static_cast<char>('0' + (magnitude % decimal));
        magnitude /= decimal;
    }
}
} // namespace

std::string rfc3339_utc(std::int64_t seconds_since_epoch) {
    std::int64_t day = seconds_since_epoch / seconds_per_day;
    std::int64_t rest = seconds_since_epoch % seconds_per_day;
    if (rest < 0) {
        rest += seconds_per_day;
        --day;
    }
    const auto date = civil_from_days(day);
    std::string text;
    append_padded(text, date.year, year_digits);
    text.push_back('-');
    append_padded(text, date.month, field_digits);
    text.push_back('-');
    append_padded(text, date.day, field_digits);
    text.push_back('T');
    append_padded(text, rest / seconds_per_hour, field_digits);
    text.push_back(':');
    append_padded(text, (rest / seconds_per_minute) % seconds_per_minute, field_digits);
    text.push_back(':');
    append_padded(text, rest % seconds_per_minute, field_digits);
    text.push_back('Z');
    return text;
}
} // namespace docenhance::host
