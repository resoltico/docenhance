// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/identity.hpp"
#include "docenhance/image/raster.hpp"
#include "read_fields.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
namespace docenhance::bundle {
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
const RecordJson& record_field(const RecordJson& object, std::string_view name) {
    for (const auto& item : object.items()) {
        if (std::string_view{item.key()} == name) {
            return item.value();
        }
    }
    return object.at(std::string(name));
}
std::uint64_t record_integer(const RecordJson& value, std::uint64_t maximum) {
    if (!value.is_number_unsigned() || value.get<std::uint64_t>() > maximum) {
        return value.at("invalid_integer").get<std::uint64_t>(); // contained type error
    }
    return value.get<std::uint64_t>();
}
double record_number(const RecordJson& value) {
    const auto result = value.get<double>();
    return std::isfinite(result) ? result : std::numeric_limits<double>::quiet_NaN();
}
bool record_boolean(const RecordJson& value) {
    return value.get<bool>();
}
std::string record_text(const RecordJson& value) {
    return value.get<std::string>();
}
bool record_hexadecimal(std::string_view value, std::size_t length) {
    return value.size() == length && std::ranges::all_of(value, [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
bool record_instant(std::string_view value) {
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
image::RasterShape record_shape(const RecordJson& value) {
    constexpr unsigned channels_max = 4;
    const auto channels = record_integer(record_field(value, "channels"), channels_max);
    constexpr auto models = std::to_array({
        image::SampleModel::gray,
        image::SampleModel::gray,
        image::SampleModel::gray_alpha,
        image::SampleModel::rgb,
        image::SampleModel::rgba,
    });
    return {
        .width =
            static_cast<std::uint32_t>(record_integer(record_field(value, "width"), UINT32_MAX)),
        .height =
            static_cast<std::uint32_t>(record_integer(record_field(value, "height"), UINT32_MAX)),
        .model = models.at(channels),
        .depth = static_cast<unsigned>(
            record_integer(record_field(value, "bit_depth"), image::word_bits)),
    };
}
std::optional<image::Resolution> record_resolution(const RecordJson& value) {
    if (value.is_null()) {
        return std::nullopt;
    }
    return image::Resolution{
        .x = static_cast<std::uint32_t>(record_integer(record_field(value, "x_ppm"), UINT32_MAX)),
        .y = static_cast<std::uint32_t>(record_integer(record_field(value, "y_ppm"), UINT32_MAX)),
    };
}
core::ContentIdentity record_identity(const RecordJson& value) {
    return {
        .sha256 = record_text(record_field(value, "sha256")),
        .bytes = record_integer(record_field(value, "bytes"), UINT64_MAX),
    };
}
} // namespace docenhance::bundle
