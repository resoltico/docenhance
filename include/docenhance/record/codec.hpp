// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/record/codec_read.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/scalar.hpp"
#include "docenhance/record/wire_type.hpp"

#include <cstddef>
#include <exception>
#include <expected>
#include <new>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace docenhance::record {
class Codec {
  public:
    template <typename T> [[nodiscard]] static core::Result<Json> encode(const T& value) {
        if constexpr (core::null_value<T>) {
            return Json(nullptr);
        } else if constexpr (WireType<T>::kind == WireKind::optional) {
            return field_value(value, std::monostate{});
        } else if constexpr (WireType<T>::kind == WireKind::sequence) {
            Json result = Json::array();
            for (std::size_t i = 0; i < value.size(); ++i) {
                auto encoded = encode<typename WireType<T>::Element>(value.at(i));
                if (!encoded) {
                    return std::unexpected(std::move(encoded.error()));
                }
                result.push_back(std::move(*encoded));
            }
            return result;
        } else if constexpr (WireType<T>::kind == WireKind::variant) {
            if (value.valueless_by_exception()) {
                return core::failure(core::ErrorCode::invariant, "Valueless typed wire variant");
            }
            return std::visit([](const auto& item) { return encode(item); }, value);
        } else if constexpr (requires { core::Fields<T>::list; }) {
            return object(value);
        } else {
            return ScalarCodec::encode(value);
        }
    }

    template <typename T> [[nodiscard]] static core::Result<T> decode(const Json& input) {
        try {
            T value{};
            if (!wire_read::read(input, value)) {
                return core::failure(core::ErrorCode::input, "Invalid typed wire value");
            }
            return value;
        } catch (const std::bad_alloc&) {
            return core::failure(core::ErrorCode::resource, "Wire allocation failed");
        } catch (const std::exception&) {
            return core::failure(core::ErrorCode::input, "Invalid typed wire value");
        }
    }

    template <typename T>
    [[nodiscard]] static core::Result<T> parse(std::string_view text, JsonLimits limits) {
        try {
            auto admitted = admit_json(text, limits);
            if (!admitted) {
                return std::unexpected(std::move(admitted.error()));
            }
            return decode<T>(Json::parse(text));
        } catch (const std::bad_alloc&) {
            return core::failure(core::ErrorCode::resource, "Wire allocation failed");
        } catch (const std::exception&) {
            return core::failure(core::ErrorCode::input, "Invalid typed wire value");
        }
    }

  private:
    template <typename T> [[nodiscard]] static core::Result<Json> object(const T& value) {
        static_assert(core::valid_field_keys<T>());
        Json result = Json::object();
        if constexpr (core::has_wire_tag<T>) {
            if (!core::valid_utf8(core::WireTag<T>::key)) {
                return core::failure(core::ErrorCode::invariant, "Invalid wire discriminator key");
            }
            auto tag = ScalarCodec::encode(std::string{core::WireTag<T>::value});
            if (!tag) {
                return std::unexpected(std::move(tag.error()));
            }
            result.update(Json{{std::string{core::WireTag<T>::key}, std::move(*tag)}});
        }
        core::Result<void> status;
        const auto append = [&](const auto& item) {
            if (status) {
                status = write_member<T>(result, value, item);
            }
        };
        std::apply([&](auto... item) { (append(item), ...); }, core::Fields<T>::list);
        if (!status) {
            return std::unexpected(std::move(status.error()));
        }
        if (!wire_read::valid_object(value)) {
            return core::failure(core::ErrorCode::invariant, "Invalid typed wire object");
        }
        return result;
    }

    template <typename T, typename Member>
    static core::Result<void> write_member(Json& output, const T& value, const Member& member) {
        if (!core::valid_utf8(member.key)) {
            return core::failure(core::ErrorCode::invariant, "Invalid typed wire field key");
        }
        const auto& field = value.*member.member;
        using Value = std::remove_cvref_t<decltype(field)>;
        if constexpr (core::sparse_fields<T> && WireType<Value>::kind == WireKind::optional) {
            if (!field) {
                return {};
            }
        }
        auto encoded = field_value(field, member.domain);
        if (!encoded) {
            return std::unexpected(std::move(encoded.error()));
        }
        if constexpr (core::sparse_fields<T>) {
            if (encoded->is_null()) {
                return core::failure(core::ErrorCode::invariant, "Null sparse typed wire field");
            }
        }
        output.update(Json{{std::string{member.key}, std::move(*encoded)}});
        return {};
    }

    template <typename T, typename Domain>
    [[nodiscard]] static core::Result<Json> field_value(const T& value, const Domain& domain) {
        if constexpr (requires {
                          domain.numeric;
                          domain.empty;
                      }) {
            static_assert(WireType<T>::kind == WireKind::optional);
            return value ? field_value(*value, domain.numeric)
                         : ScalarCodec::encode(std::string{domain.empty});
        } else if constexpr (WireType<T>::kind == WireKind::optional) {
            if (!value) {
                return Json(nullptr);
            }
            auto encoded = field_value(*value, domain);
            if (!encoded) {
                return std::unexpected(std::move(encoded.error()));
            }
            if (encoded->is_null()) {
                return core::failure(core::ErrorCode::invariant, "Redundant nullable wire value");
            }
            return encoded;
        } else {
            if (!ScalarCodec::valid(value, domain)) {
                return core::failure(core::ErrorCode::invariant, "Invalid typed wire field domain");
            }
            return encode(value);
        }
    }
};
} // namespace docenhance::record
