// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/identity.hpp"

#include "docenhance/version.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <limits>
#include <string_view>

namespace docenhance::core {
BuildFacts build_facts() noexcept {
    return {
        .version = application_version,
        .platform = build_platform,
        .compiler = build_compiler,
        .dependency_lock_sha256 = dependency_lock_sha256,
    };
}
namespace {
constexpr std::size_t year_digits = 4;
constexpr std::size_t month_separator = 7;
constexpr std::size_t date_separator = 10;
constexpr std::size_t hour_separator = 13;
constexpr std::size_t minute_separator = 16;
constexpr std::size_t month_start = 5;
constexpr std::size_t day_start = 8;
constexpr std::size_t hour_start = 11;
constexpr std::size_t minute_start = 14;
constexpr std::size_t second_start = 17;
constexpr unsigned decimal_base = 10;
constexpr unsigned last_month = 12;
constexpr unsigned last_day = 31;
} // namespace
bool valid_hexadecimal(std::string_view value, std::size_t length) {
    return value.size() == length && std::ranges::all_of(value, [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
bool valid_instant(std::string_view value) {
    constexpr std::size_t instant_length = 20;
    if (value.size() != instant_length || value.substr(year_digits, 1) != "-" ||
        value.substr(month_separator, 1) != "-" || value.substr(date_separator, 1) != "T" ||
        value.substr(hour_separator, 1) != ":" || value.substr(minute_separator, 1) != ":" ||
        value.back() != 'Z') {
        return false;
    }
    const auto digits = [&](std::size_t at, std::size_t count) -> unsigned {
        unsigned result = 0;
        for (char const c : value.substr(at, count)) {
            if (c < '0' || c > '9') {
                return std::numeric_limits<unsigned>::max();
            }
            result = (result * decimal_base) + static_cast<unsigned>(c - '0');
        }
        return result;
    };
    const auto year = digits(0, year_digits);
    const auto month = digits(month_start, 2);
    const auto day = digits(day_start, 2);
    constexpr unsigned last_year = 9999;
    constexpr unsigned hours = 24;
    constexpr unsigned minutes = 60;
    if (year == 0 || year > last_year || month > last_month || day > last_day) {
        return false;
    }
    const std::chrono::year_month_day date{std::chrono::year{static_cast<int>(year)},
                                           std::chrono::month{month}, std::chrono::day{day}};
    return date.ok() && digits(hour_start, 2) < hours && digits(minute_start, 2) < minutes &&
           digits(second_start, 2) < minutes;
}
} // namespace docenhance::core
