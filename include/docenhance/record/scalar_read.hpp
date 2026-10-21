// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/record/json.hpp"

#include <cmath>
#include <concepts>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace docenhance::record::scalar_read {
template <typename T> [[nodiscard]] bool integer(const Json& input, T& output) {
    if (input.is_number_unsigned()) {
        const auto value = input.template get<std::uint64_t>();
        if (!std::in_range<T>(value)) {
            return false;
        }
        output = static_cast<T>(value);
        return true;
    }
    if (!input.is_number_integer()) {
        return false;
    }
    const auto value = input.template get<std::int64_t>();
    if (!std::in_range<T>(value)) {
        return false;
    }
    output = static_cast<T>(value);
    return true;
}

template <typename T> [[nodiscard]] bool real(const Json& input, T& output) {
    if (!input.is_number()) {
        return false;
    }
    const auto value = input.template get<double>();
    if (!std::isfinite(value) || value < std::numeric_limits<T>::lowest() ||
        value > std::numeric_limits<T>::max()) {
        return false;
    }
    output = static_cast<T>(value);
    return true;
}

[[nodiscard]] inline bool text(const Json& input, std::string& output) {
    if (!input.is_string()) {
        return false;
    }
    const auto& value = input.template get_ref<const Json::string_t&>();
    if (!core::valid_utf8(value)) {
        return false;
    }
    output = value;
    return true;
}

template <typename T> [[nodiscard]] bool enumeration(const Json& input, T& output) {
    if (!input.is_string()) {
        return false;
    }
    const auto& name = input.template get_ref<const Json::string_t&>();
    for (const auto& [item, spelling] : core::EnumNames<T>::list) {
        if (name == spelling) {
            output = item;
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline std::optional<image::SampleModel> sample_model(unsigned value) {
    for (const auto model : {
             image::SampleModel::gray,
             image::SampleModel::gray_alpha,
             image::SampleModel::rgb,
             image::SampleModel::rgba,
         }) {
        if (image::components(model) == value) {
            return std::optional{model};
        }
    }
    return std::optional<image::SampleModel>{};
}

template <typename T> [[nodiscard]] bool admitted_integer(const Json& input, T& output) {
    unsigned value{};
    if (!scalar_read::integer(input, value)) {
        return false;
    }
    const auto admitted = [&] {
        if constexpr (std::same_as<T, image::Orientation>) {
            return T::from_code(value);
        } else if constexpr (std::same_as<T, image::QuarterTurn>) {
            return T::from_degrees(value);
        } else if constexpr (std::same_as<T, image::SampleModel>) {
            return scalar_read::sample_model(value);
        } else {
            return T::from_bits(value);
        }
    }();
    if (!admitted) {
        return false;
    }
    output = *admitted;
    return true;
}

template <typename T> [[nodiscard]] bool decode(const Json& input, T& output) {
    if constexpr (std::same_as<T, image::SampleDepth> || std::same_as<T, image::PngDepth> ||
                  std::same_as<T, image::Orientation> || std::same_as<T, image::QuarterTurn> ||
                  std::same_as<T, image::SampleModel>) {
        return scalar_read::admitted_integer(input, output);
    } else if constexpr (std::is_enum_v<T>) {
        return scalar_read::enumeration(input, output);
    } else if constexpr (std::same_as<T, std::string>) {
        return scalar_read::text(input, output);
    } else if constexpr (std::same_as<T, bool>) {
        if (!input.is_boolean()) {
            return false;
        }
        output = input.template get<bool>();
        return true;
    } else if constexpr (std::integral<T>) {
        return scalar_read::integer(input, output);
    } else {
        static_assert(std::floating_point<T>);
        return scalar_read::real(input, output);
    }
}

template <typename T> [[nodiscard]] core::Result<Json> image_schema() {
    if constexpr (std::same_as<T, image::SampleDepth>) {
        return Json{{"type", "integer"}, {"enum", {image::byte_bits, image::word_bits}}};
    } else if constexpr (std::same_as<T, image::PngDepth>) {
        return Json{{"type", "integer"}, {"enum", {1, image::byte_bits, image::word_bits}}};
    } else if constexpr (std::same_as<T, image::Orientation>) {
        Json choices = Json::array();
        for (unsigned code = 1; image::Orientation::from_code(code); ++code) {
            choices.push_back(code);
        }
        return Json{{"type", "integer"}, {"enum", std::move(choices)}};
    } else if constexpr (std::same_as<T, image::QuarterTurn>) {
        return Json{
            {"type", "integer"},
            {
                "enum",
                {
                    0,
                    image::QuarterTurn::quarter_degrees,
                    image::QuarterTurn::half_degrees,
                    image::QuarterTurn::three_quarter_degrees,
                },
            },
        };
    } else {
        static_assert(std::same_as<T, image::SampleModel>);
        return Json{
            {"type", "integer"},
            {
                "enum",
                {
                    image::components(image::SampleModel::gray),
                    image::components(image::SampleModel::gray_alpha),
                    image::components(image::SampleModel::rgb),
                    image::components(image::SampleModel::rgba),
                },
            },
        };
    }
}
} // namespace docenhance::record::scalar_read
