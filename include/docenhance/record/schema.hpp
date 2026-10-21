// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/core/range.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/scalar.hpp"
#include "docenhance/record/wire_type.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace docenhance::record {
class Schema {
  public:
    template <typename T> [[nodiscard]] static core::Result<Json> of();

  private:
    template <typename T> [[nodiscard]] static core::Result<Json> variant_schema();
    [[nodiscard]] static core::Result<void> collection(Json& value,
                                                       core::Range<std::size_t> domain);
    template <typename T> [[nodiscard]] static core::Result<Json> object();
    template <typename T, typename Domain>
    [[nodiscard]] static core::Result<Json> field(const Domain& domain);
    template <typename T, typename Domain>
    [[nodiscard]] static core::Result<Json> scalar_choice(const Domain& domain);
    template <bool Sparse, typename Value, typename Domain>
    [[nodiscard]] static core::Result<void>
    describe_field(Json& properties, Json& required, std::string_view key, const Domain& domain);
};
template <typename T> core::Result<Json> Schema::variant_schema() {
    Json alternatives = Json::array();
    core::Result<void> status;
    const auto append = [&]<typename Alternative> {
        if (!status) {
            return;
        }
        auto schema = of<Alternative>();
        if (!schema) {
            status = std::unexpected(std::move(schema.error()));
            return;
        }
        alternatives.push_back(std::move(*schema));
    };
    [&]<std::size_t... Index>(std::index_sequence<Index...> /*indices*/) {
        (append.template operator()<std::variant_alternative_t<Index, T>>(), ...);
    }(std::make_index_sequence<std::variant_size_v<T>>{});
    if (!status) {
        return std::unexpected(std::move(status.error()));
    }
    return Json{{"oneOf", std::move(alternatives)}};
}

template <typename T> core::Result<Json> Schema::of() {
    if constexpr (core::null_value<T>) {
        return Json{{"type", "null"}};
    } else if constexpr (WireType<T>::kind == WireKind::optional) {
        auto element = of<typename WireType<T>::Element>();
        if (!element) {
            return std::unexpected(std::move(element.error()));
        }
        return Json{{"anyOf", Json::array({std::move(*element), {{"type", "null"}}})}};
    } else if constexpr (WireType<T>::kind == WireKind::sequence) {
        auto element = of<typename WireType<T>::Element>();
        if (!element) {
            return std::unexpected(std::move(element.error()));
        }
        Json result{{"type", "array"}, {"items", std::move(*element)}};
        if constexpr (WireType<T>::fixed) {
            result.update(Json{{"minItems", WireType<T>::size}});
            result.update(Json{{"maxItems", WireType<T>::size}});
        }
        return result;
    } else if constexpr (WireType<T>::kind == WireKind::variant) {
        return variant_schema<T>();
    } else if constexpr (requires { core::Fields<T>::list; }) {
        return object<T>();
    } else {
        return ScalarCodec::schema<T>();
    }
}

inline core::Result<void> Schema::collection(Json& value, core::Range<std::size_t> domain) {
    if (domain.odd) {
        return core::failure(core::ErrorCode::invariant,
                             "Odd cardinality is not a supported wire collection domain");
    }
    if (!value.contains("minItems") || value.at("minItems") < domain.minimum) {
        value.update(Json{{"minItems", domain.minimum}});
    }
    if (!value.contains("maxItems") || value.at("maxItems") > domain.maximum) {
        value.update(Json{{"maxItems", domain.maximum}});
    }
    return {};
}

template <typename T, typename Domain>
core::Result<Json> Schema::scalar_choice(const Domain& domain) {
    static_assert(WireType<T>::kind == WireKind::optional);
    using Value = WireType<T>::Element;
    auto numeric = field<Value>(domain.numeric);
    if (!numeric) {
        return std::unexpected(std::move(numeric.error()));
    }
    auto empty = ScalarCodec::encode(std::string{domain.empty});
    if (!empty) {
        return std::unexpected(std::move(empty.error()));
    }
    return Json{
        {
            "oneOf",
            Json::array({
                std::move(*numeric),
                {{"type", "string"}, {"const", std::move(*empty)}},
            }),
        },
    };
}

template <typename T, typename Domain> core::Result<Json> Schema::field(const Domain& domain) {
    if constexpr (requires {
                      domain.numeric;
                      domain.empty;
                  }) {
        return scalar_choice<T>(domain);
    } else if constexpr (WireType<T>::kind == WireKind::optional) {
        auto element = field<typename WireType<T>::Element>(domain);
        if (!element) {
            return std::unexpected(std::move(element.error()));
        }
        return Json{{"anyOf", Json::array({std::move(*element), {{"type", "null"}}})}};
    } else {
        auto result = of<T>();
        if (!result) {
            return std::unexpected(std::move(result.error()));
        }
        if constexpr (WireType<T>::kind == WireKind::sequence &&
                      std::is_same_v<Domain, core::Range<std::size_t>>) {
            auto constrained = collection(*result, domain);
            if (!constrained) {
                return std::unexpected(std::move(constrained.error()));
            }
        } else {
            auto constrained = ScalarCodec::constrain(*result, domain);
            if (!constrained) {
                return std::unexpected(std::move(constrained.error()));
            }
        }
        return result;
    }
}

template <bool Sparse, typename Value, typename Domain>
core::Result<void> Schema::describe_field(Json& properties, Json& required, std::string_view key,
                                          const Domain& domain) {
    if (!core::valid_utf8(key)) {
        return core::failure(core::ErrorCode::invariant, "Invalid wire field UTF-8");
    }
    if constexpr (Sparse && WireType<Value>::kind == WireKind::optional) {
        auto element = field<typename WireType<Value>::Element>(domain);
        if (!element) {
            return std::unexpected(std::move(element.error()));
        }
        properties.update(Json{
            {
                std::string{key},
                {
                    {"allOf", Json::array({std::move(*element), {{"not", {{"type", "null"}}}}})},
                },
            },
        });
    } else {
        auto element = field<Value>(domain);
        if (!element) {
            return std::unexpected(std::move(element.error()));
        }
        properties.update(Json{{std::string{key}, std::move(*element)}});
        required.push_back(std::string{key});
    }
    return {};
}

template <typename T> core::Result<Json> Schema::object() {
    static_assert(core::valid_field_keys<T>());
    Json properties = Json::object();
    Json required = Json::array();
    if constexpr (core::has_wire_tag<T>) {
        if (!core::valid_utf8(core::WireTag<T>::key)) {
            return core::failure(core::ErrorCode::invariant, "Invalid wire tag UTF-8");
        }
        auto tag = ScalarCodec::encode(std::string{core::WireTag<T>::value});
        if (!tag) {
            return std::unexpected(std::move(tag.error()));
        }
        properties.update(Json{
            {
                std::string{core::WireTag<T>::key},
                {
                    {"type", "string"},
                    {"const", std::move(*tag)},
                },
            },
        });
        required.push_back(std::string{core::WireTag<T>::key});
    }
    core::Result<void> status;
    const auto append = [&](auto descriptor) {
        if (status) {
            using Value = std::remove_cvref_t<decltype(std::declval<T&>().*descriptor.member)>;
            status = describe_field<core::sparse_fields<T>, Value>(
                properties, required, descriptor.key, descriptor.domain);
        }
    };
    std::apply([&](auto... descriptor) { (append(descriptor), ...); }, core::Fields<T>::list);
    if (!status) {
        return std::unexpected(std::move(status.error()));
    }
    return Json{
        {"type", "object"},
        {"properties", std::move(properties)},
        {"required", std::move(required)},
        {"additionalProperties", false},
    };
}
} // namespace docenhance::record
