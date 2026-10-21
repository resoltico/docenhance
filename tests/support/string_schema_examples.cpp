// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "string_schema_examples.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
#include "schema_example.hpp"
#include "string_fields_fixture.hpp"

#include <array>
#include <expected>
#include <string>
#include <utility>
namespace docenhance::tests {
namespace {
record::Json invalid_string_domains(const record::Json& encoded) {
    auto invalid = record::Json::array();
    for (const auto* const key : {"text", "instant", "path", "literal"}) {
        for (const auto& bad : record::Json::array({"", true, 1, nullptr})) {
            auto changed = encoded;
            changed[key] = bad;
            invalid.push_back(std::move(changed));
        }
    }
    for (const auto* const path :
         {"/a", "a/", "a//b", ".", "..", "a/./b", "a/../b", "a:b", "a\\b", "a\n//b"}) {
        auto changed = encoded;
        changed["path"] = path;
        invalid.push_back(std::move(changed));
    }
    for (const auto* const key : {"text", "path"}) {
        auto changed = encoded;
        changed[key] = std::string{"a\0b", 3};
        invalid.push_back(std::move(changed));
    }
    auto changed = encoded;
    changed["instant"] = "2024-02-29T23:59:59Z\n";
    invalid.push_back(std::move(changed));
    changed = encoded;
    changed["literal"] = "different";
    invalid.push_back(std::move(changed));
    changed = encoded;
    changed["path"] = std::string(129, 'a');
    invalid.push_back(std::move(changed));
    changed = encoded;
    changed["unknown"] = "owned";
    invalid.push_back(std::move(changed));
    return invalid;
}
} // namespace
core::Result<void> string_schema_examples(record::Json& cases) {
    const WireStrings value;
    auto encoded_result = record::Codec::encode(value);
    if (!encoded_result) {
        return std::unexpected(std::move(encoded_result.error()));
    }
    const auto encoded = std::move(*encoded_result);
    auto invalid = invalid_string_domains(encoded);
    if (auto added = schema_example(cases, "string domains", value, std::move(invalid)); !added) {
        return added;
    }
    for (const auto& [name, path] : std::array{
             std::pair{"LF dot suffix", "a/..\n"},
             std::pair{"LF component", "a\n/b"},
             std::pair{"LF only", "\n"},
             std::pair{"three dots", "..."},
             std::pair{"LF dot prefix", "a/.\nb"},
         }) {
        WireStrings healthy;
        healthy.path = path;
        if (auto added = schema_example(cases, name, healthy); !added) {
            return added;
        }
    }
    WireStrings boundary;
    boundary.path.clear();
    for (unsigned i = 0; i < 64; ++i) {
        boundary.path += "\xc3\xa9";
    }
    if (auto added = schema_example(cases, "128 UTF8 byte path", boundary); !added) {
        return added;
    }
    // 65 codepoints would satisfy schema maxLength128 but violate native128 bytes. The native
    // test keeps this refusal separate; schema_example requires a native round trip.
    // Calendar validity likewise remains native; the schema annotates rather than asserts format.
    const WireEmptyLiteral empty_literal{};
    if (auto added = schema_example(cases, "empty literal", empty_literal,
                                    record::Json::array({{{"text", "different"}}}));
        !added) {
        return added;
    }
    return {};
}
} // namespace docenhance::tests
