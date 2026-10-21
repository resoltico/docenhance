// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/schema.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace docenhance::tests {
template <typename T>
[[nodiscard]] core::Result<void> schema_example(record::Json& cases, std::string_view name,
                                                const T& value,
                                                record::Json invalid = record::Json::array()) {
    auto schema = record::Schema::of<T>();
    if (!schema) {
        return std::unexpected(std::move(schema.error()));
    }
    auto encoded = record::Codec::encode(value);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    const auto decoded = record::Codec::decode<T>(*encoded);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    if (*decoded != value) {
        return core::failure(core::ErrorCode::invariant,
                             "The emitted schema fixture did not round trip");
    }
    (*schema)["$schema"] = "https://json-schema.org/draft/2020-12/schema";
    cases.push_back({
        {"name", std::string{name}},
        {"schema", std::move(*schema)},
        {"value", std::move(*encoded)},
        {"invalid", std::move(invalid)},
    });
    return {};
}
} // namespace docenhance::tests
