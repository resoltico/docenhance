// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/bundle_path.hpp"
#include "docenhance/core/fields.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/range.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/scalar_read.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <expected>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace docenhance::record {
class ScalarCodec {
  public:
    template <typename T, typename Domain>
    [[nodiscard]] static bool valid(const T& value, const Domain& domain) {
        if constexpr (std::same_as<Domain, std::monostate>) {
            if constexpr (std::floating_point<T>) {
                return std::isfinite(value);
            } else {
                return true;
            }
        } else if constexpr (std::same_as<Domain, core::Count>) {
            static_assert(std::integral<T> && !std::same_as<T, bool>);
            return std::cmp_greater_equal(value, 0) &&
                   std::cmp_less_equal(value, image::source_pixels_max);
        } else if constexpr (std::same_as<Domain, core::Unit>) {
            static_assert(std::floating_point<T>);
            return core::in_range(value, T{0}, T{1});
        } else if constexpr (std::same_as<Domain, core::Text>) {
            static_assert(std::same_as<T, std::string>);
            return core::valid_path(value);
        } else if constexpr (std::same_as<Domain, core::Instant>) {
            static_assert(std::same_as<T, std::string>);
            return core::valid_instant(value);
        } else if constexpr (std::same_as<Domain, core::BundlePath>) {
            static_assert(std::same_as<T, std::string>);
            return core::valid_bundle_path(value);
        } else if constexpr (std::same_as<Domain, core::Literal>) {
            static_assert(std::same_as<T, std::string>);
            return core::valid_utf8(domain.text) && value == domain.text;
        } else if constexpr (std::same_as<Domain, core::Hex>) {
            static_assert(std::same_as<T, std::string>);
            return valid_hex(value, domain);
        } else if constexpr (std::same_as<Domain, core::Range<std::size_t>> &&
                             requires { value.size(); }) {
            return domain.contains(value.size());
        } else {
            return domain.contains(value);
        }
    }

    template <typename T> [[nodiscard]] static core::Result<Json> encode(const T& value) {
        if constexpr (std::same_as<T, image::SampleDepth> || std::same_as<T, image::PngDepth>) {
            return value.bits();
        } else if constexpr (std::same_as<T, image::Orientation>) {
            return value.code();
        } else if constexpr (std::same_as<T, image::QuarterTurn>) {
            return value.degrees();
        } else if constexpr (std::same_as<T, image::SampleModel>) {
            const auto channels = image::components(value);
            if (channels == 0) {
                return core::failure(core::ErrorCode::invariant, "Unknown sample model");
            }
            return channels;
        } else if constexpr (std::is_enum_v<T>) {
            for (const auto& [item, name] : core::EnumNames<T>::list) {
                if (item == value) {
                    return encode(std::string{name});
                }
            }
            return core::failure(core::ErrorCode::invariant, "Unknown wire enum value");
        } else if constexpr (std::same_as<T, std::string>) {
            if (!core::valid_utf8(value)) {
                return core::failure(core::ErrorCode::invariant, "Invalid wire UTF-8");
            }
            return value;
        } else {
            static_assert(std::is_arithmetic_v<T>);
            if (!valid(value, std::monostate{})) {
                return core::failure(core::ErrorCode::invariant, "Nonfinite wire number");
            }
            return value;
        }
    }

    template <typename T> [[nodiscard]] static bool decode(const Json& input, T& output) {
        return scalar_read::decode(input, output);
    }

    template <typename T> [[nodiscard]] static core::Result<Json> schema() {
        if constexpr (std::same_as<T, image::SampleDepth> || std::same_as<T, image::PngDepth> ||
                      std::same_as<T, image::Orientation> || std::same_as<T, image::QuarterTurn> ||
                      std::same_as<T, image::SampleModel>) {
            return scalar_read::image_schema<T>();
        } else if constexpr (std::is_enum_v<T>) {
            Json choices = Json::array();
            for (const auto& [item, name] : core::EnumNames<T>::list) {
                static_cast<void>(item);
                auto encoded = encode(std::string{name});
                if (!encoded) {
                    return std::unexpected(std::move(encoded.error()));
                }
                choices.push_back(std::move(*encoded));
            }
            return Json{{"type", "string"}, {"enum", std::move(choices)}};
        } else if constexpr (std::same_as<T, std::string>) {
            return Json{{"type", "string"}};
        } else if constexpr (std::same_as<T, bool>) {
            return Json{{"type", "boolean"}};
        } else if constexpr (std::integral<T>) {
            return Json{
                {"type", "integer"},
                {"minimum", std::numeric_limits<T>::lowest()},
                {"maximum", std::numeric_limits<T>::max()},
            };
        } else {
            static_assert(std::floating_point<T>);
            return Json{
                {"type", "number"},
                {"minimum", std::numeric_limits<T>::lowest()},
                {"maximum", std::numeric_limits<T>::max()},
            };
        }
    }

    template <typename Domain>
    [[nodiscard]] static core::Result<void> constrain(Json& value, const Domain& domain) {
        if constexpr (std::same_as<Domain, core::Count>) {
            bound(value, "minimum", 0, true);
            bound(value, "maximum", image::source_pixels_max, false);
        } else if constexpr (std::same_as<Domain, core::Unit>) {
            bound(value, "minimum", 0, true);
            bound(value, "maximum", 1, false);
        } else if constexpr (std::same_as<Domain, core::Text>) {
            value.update(Json{{"minLength", 1}});
            value.update(Json{{"pattern", core::text_pattern}});
        } else if constexpr (std::same_as<Domain, core::Instant>) {
            value.update(Json{{"minLength", core::instant_length}});
            value.update(Json{{"maxLength", core::instant_length}});
            value.update(Json{{"pattern", core::instant_pattern}});
            value.update(Json{{"format", "date-time"}});
        } else if constexpr (std::same_as<Domain, core::BundlePath>) {
            value.update(Json{{"minLength", 1}});
            // A necessary character bound; native admission also enforces UTF-8 byte length.
            value.update(Json{{"maxLength", core::bundle_path_max_bytes}});
            value.update(Json{{"pattern", core::bundle_path_pattern}});
        } else if constexpr (std::same_as<Domain, core::Literal>) {
            auto encoded = encode(std::string{domain.text});
            if (!encoded) {
                return std::unexpected(std::move(encoded.error()));
            }
            value.update(Json{{"const", std::move(*encoded)}});
        } else if constexpr (std::same_as<Domain, core::Hex>) {
            value.update(Json{{"minLength", domain.digits}});
            value.update(Json{{"maxLength", domain.digits}});
            value.update(Json{{"pattern", "^[0-9a-f]{" + std::to_string(domain.digits) + "}$"}});
        } else if constexpr (requires { domain.value; }) {
            auto encoded = encode(domain.value);
            if (!encoded) {
                return std::unexpected(std::move(encoded.error()));
            }
            value.update(Json{{"const", std::move(*encoded)}});
        } else if constexpr (requires { domain.values; }) {
            return choice_constraint(value, domain);
        } else if constexpr (!std::same_as<Domain, std::monostate>) {
            bound(value, "minimum", domain.minimum, true);
            bound(value, "maximum", domain.maximum, false);
            if constexpr (std::integral<decltype(domain.minimum)>) {
                if (domain.odd) {
                    value.update(Json{{"not", {{"multipleOf", 2}}}});
                }
            }
        }
        return {};
    }

  private:
    [[nodiscard]] static bool valid_hex(const std::string& value, core::Hex domain) {
        if (value.size() != domain.digits) {
            return false;
        }
        return std::ranges::all_of(value, [](char digit) {
            return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
        });
    }

    template <typename Domain>
    [[nodiscard]] static core::Result<void> choice_constraint(Json& value, const Domain& domain) {
        Json choices = Json::array();
        for (const auto item : domain.values) {
            auto encoded = encode(item);
            if (!encoded) {
                return std::unexpected(std::move(encoded.error()));
            }
            if (std::ranges::find(choices, *encoded) != choices.end()) {
                return core::failure(core::ErrorCode::invariant, "Duplicate encoded wire choice");
            }
            choices.push_back(std::move(*encoded));
        }
        value.update(Json{{"enum", std::move(choices)}});
        return {};
    }

    static void bound(Json& description, const char* key, const Json& value, bool minimum) {
        const auto present = description.find(key);
        if (present == description.end() || (minimum ? value > *present : value < *present)) {
            description.update(Json{{key, value}});
        }
    }
};
} // namespace docenhance::record
