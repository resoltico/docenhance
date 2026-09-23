// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/contract/utf8.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace docenhance::tests {
static_assert(contract::valid_utf8("A\xc4\x80\xf0\x9f\x93\x84"));
static_assert(!contract::valid_utf8("\xc0\x80"));
namespace {
struct EncodedScalar {
    std::array<char, 4> bytes{};
    std::size_t size = 0;
    [[nodiscard]] std::string_view text() const noexcept {
        return {bytes.data(), size};
    }
};
// Independent code-point construction, not the validator's byte-range state machine.
EncodedScalar encode(std::uint32_t value) {
    EncodedScalar encoded;
    const auto push = [&encoded](std::uint32_t byte) {
        encoded.bytes.at(encoded.size++) = static_cast<char>(static_cast<unsigned char>(byte));
    };
    if (value <= 0x7f) {
        push(value);
    } else if (value <= 0x7ff) {
        push(0xc0U | (value >> 6U));
        push(0x80U | (value & 0x3fU));
    } else if (value <= 0xffff) {
        push(0xe0U | (value >> 12U));
        push(0x80U | ((value >> 6U) & 0x3fU));
        push(0x80U | (value & 0x3fU));
    } else {
        push(0xf0U | (value >> 18U));
        push(0x80U | ((value >> 12U) & 0x3fU));
        push(0x80U | ((value >> 6U) & 0x3fU));
        push(0x80U | (value & 0x3fU));
    }
    return encoded;
}
} // namespace
TEST_CASE("UTF-8 admits every scalar and rejects surrogate encodings") {
    for (std::uint32_t value = 0; value <= 0x10ffff; ++value) {
        const auto encoded = encode(value);
        const bool scalar = value < 0xd800 || value > 0xdfff;
        if (contract::valid_utf8(encoded.text()) != scalar) {
            FAIL("Scalar encoding mismatch at " << value);
        }
    }
    REQUIRE(contract::valid_utf8(""));
    REQUIRE(contract::valid_utf8(std::string_view{"\0", 1}));
    REQUIRE(contract::valid_utf8("A\xc4\x80\xe2\x82\xac\xf0\x9f\x93\x84"));
}
TEST_CASE("UTF-8 rejects invalid ranges and every truncated multi-byte scalar") {
    const auto invalid = std::to_array<std::string_view>({
        "\x80",
        "\xbf",
        "\xc0\x80",
        "\xc1\xbf",
        "\xc2\x7f",
        "\xc2\xc0",
        "\xe0\x9f\xbf",
        "\xed\xa0\x80",
        "\xed\xbf\xbf",
        "\xf0\x8f\xbf\xbf",
        "\xf4\x90\x80\x80",
        "\xf5\x80\x80\x80",
        "\xf8\x88\x80\x80\x80",
        "\xfe",
        "\xff",
        "\xc2",
        "\xe1\x80",
        "\xf1\x80\x80",
        "\xe1\x80!",
        "\xf1\x80\x80!",
        "valid\x80",
        "\xef\xbf\xbf\x80",
    });
    for (const auto text : invalid) {
        REQUIRE(!contract::valid_utf8(text));
    }
    for (std::uint32_t value = 0x80; value <= 0x10ffff; ++value) {
        const auto encoded = encode(value);
        for (std::size_t length = 1; length < encoded.size; ++length) {
            if (contract::valid_utf8(encoded.text().substr(0, length))) {
                FAIL("Truncated scalar accepted at " << value << " length " << length);
            }
        }
    }
}
} // namespace docenhance::tests
