// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/contract/utf8.hpp"

#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>

namespace {
// Independent UTF-8 oracle: quote syntax/control bytes, then ask the real strict JSON parser.
// Do not serialize through json::dump, whose treatment of malformed strings could hide the input.
std::string json_string_token(std::string_view text) {
    constexpr std::string_view hex = "0123456789abcdef";
    std::string document = "\"";
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20) {
            document += "\\u00";
            document += hex.at(byte >> 4U);
            document += hex.at(byte & 0xfU);
        } else {
            if (character == '"' || character == '\\') {
                document += '\\';
            }
            document += character;
        }
    }
    document += '"';
    return document;
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    docenhance::fuzz::FuzzInput input{std::span<const std::uint8_t>{data, size}};
    constexpr std::size_t max_text_bytes = 4096;
    const auto text = input.rest(max_text_bytes);
    const auto parsed = nlohmann::json::parse(json_string_token(text), nullptr, false);
    const bool valid = docenhance::contract::valid_utf8(text);
    docenhance::fuzz::require(valid == !parsed.is_discarded(),
                              "UTF-8 validation agrees with an independent strict decoder");
    if (valid) {
        docenhance::fuzz::require(parsed.get<std::string>() == text,
                                  "Admitted text preserves its byte identity");
    }
    return 0;
}
