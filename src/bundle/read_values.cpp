// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/identity.hpp"
#include "docenhance/image/raster.hpp"
#include "read_fields.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
namespace docenhance::bundle {
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
