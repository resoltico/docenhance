// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace docenhance::fuzz {
// Independent byte oracle; malformed framing also must be E_INPUT. The encoded ceiling is
// checked before shape admission, so this property deliberately excludes larger inputs.
[[nodiscard]] inline bool zero_png_dimension(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t signature_size = 8;
    constexpr std::size_t integer_size = 4;
    constexpr std::size_t header_minimum = 33;
    constexpr std::size_t encoded_ceiling = 65536;
    constexpr std::size_t name_offset = 12;
    constexpr std::size_t width_offset = 16;
    constexpr std::size_t height_offset = 20;
    constexpr std::uint8_t signature_high = 137;
    constexpr std::uint8_t signature_control = 26;
    constexpr std::array<std::uint8_t, signature_size> signature{
        signature_high, 'P', 'N', 'G', '\r', '\n', signature_control, '\n',
    };
    constexpr std::array<std::uint8_t, integer_size> ihdr{'I', 'H', 'D', 'R'};
    if (bytes.size() < header_minimum || bytes.size() > encoded_ceiling ||
        !std::ranges::equal(bytes.first(signature_size), signature) ||
        !std::ranges::equal(bytes.subspan(name_offset, integer_size), ihdr)) {
        return false;
    }
    const auto zero = [](auto values) {
        return std::ranges::all_of(values, [](auto value) { return value == 0; });
    };
    return zero(bytes.subspan(width_offset, integer_size)) ||
           zero(bytes.subspan(height_offset, integer_size));
}
} // namespace docenhance::fuzz
