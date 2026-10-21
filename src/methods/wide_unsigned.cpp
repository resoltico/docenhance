// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "wide_unsigned.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
namespace docenhance::methods {
namespace {
constexpr unsigned limb_bits = 64;
constexpr unsigned half_bits = 32;
constexpr unsigned precision = 53;
constexpr std::uint64_t half_mask = (std::uint64_t{1} << half_bits) - 1;
std::uint64_t shifted(WideUnsigned value, unsigned shift) noexcept {
    if (shift >= limb_bits) {
        return value.high >> (shift - limb_bits);
    }
    return (value.low >> shift) | (value.high << (limb_bits - shift));
}
bool low_bits(WideUnsigned value, unsigned count) noexcept {
    if (count == 0) {
        return false;
    }
    if (count >= limb_bits) {
        return value.low != 0 ||
               (count > limb_bits &&
                (value.high & ((std::uint64_t{1} << (count - limb_bits)) - 1)) != 0);
    }
    return (value.low & ((std::uint64_t{1} << count) - 1)) != 0;
}
bool bit(WideUnsigned value, unsigned position) noexcept {
    return position >= limb_bits ? ((value.high >> (position - limb_bits)) & std::uint64_t{1}) != 0
                                 : ((value.low >> position) & std::uint64_t{1}) != 0;
}
} // namespace
WideUnsigned wide_multiply(std::uint64_t left, std::uint64_t right) noexcept {
    const auto p00 = (left & half_mask) * (right & half_mask);
    const auto p01 = (left & half_mask) * (right >> half_bits);
    const auto p10 = (left >> half_bits) * (right & half_mask);
    const auto p11 = (left >> half_bits) * (right >> half_bits);
    const auto middle = (p00 >> half_bits) + (p01 & half_mask) + (p10 & half_mask);
    return {
        .high = p11 + (p01 >> half_bits) + (p10 >> half_bits) + (middle >> half_bits),
        .low = (middle << half_bits) | (p00 & half_mask),
    };
}
std::optional<WideUnsigned> wide_subtract(WideUnsigned left, WideUnsigned right) noexcept {
    if (left.high < right.high || (left.high == right.high && left.low < right.low)) {
        return std::nullopt;
    }
    return WideUnsigned{
        .high = left.high - right.high - static_cast<std::uint64_t>(left.low < right.low),
        .low = left.low - right.low,
    };
}
double wide_double(WideUnsigned value) noexcept {
    const auto length = value.high != 0
                            ? limb_bits + static_cast<unsigned>(std::bit_width(value.high))
                            : static_cast<unsigned>(std::bit_width(value.low));
    if (length <= precision) {
        return static_cast<double>(value.low);
    }
    const auto shift = static_cast<unsigned>(length - precision);
    auto significand = shifted(value, shift);
    if (bit(value, shift - 1) &&
        (low_bits(value, shift - 1) || (significand & std::uint64_t{1}) != 0)) {
        ++significand;
    }
    return std::ldexp(static_cast<double>(significand), static_cast<int>(shift));
}
} // namespace docenhance::methods
