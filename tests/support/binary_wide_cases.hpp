// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "../../src/methods/wide_unsigned.hpp"
#include "require.hpp"

#include <cstdint>
#include <limits>
namespace docenhance::tests {
inline void binary_wide_cases() {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    require(methods::wide_multiply(max, max) == methods::WideUnsigned{.high = max - 1, .low = 1},
            "exact full64 product carries into high limb");
    require(methods::wide_subtract({.high = 1, .low = 0}, {.high = 0, .low = 1}) ==
                methods::WideUnsigned{.high = 0, .low = max},
            "exact subtraction borrows across limb boundary");
    constexpr methods::WideUnsigned one{.high = 0, .low = 1};
    constexpr methods::WideUnsigned zero{};
    require(!methods::wide_subtract(zero, one), "unsigned negative subtraction is rejected");
    require(methods::wide_double(
                {.high = 0, .low = (std::uint64_t{1} << 53U) + std::uint64_t{1}}) == 0x1p53 &&
                methods::wide_double(
                    {.high = 0, .low = (std::uint64_t{1} << 53U) + std::uint64_t{3}}) ==
                    0x1.0000000000002p53,
            "independently authored even and odd low-limb ties round once");
    require(methods::wide_double({.high = 1, .low = 2048}) == 0x1p64 &&
                methods::wide_double({.high = 1, .low = 2049}) == 0x1.0000000000001p64 &&
                methods::wide_double({.high = max, .low = max}) == 0x1p128,
            "cross-limb guard sticky and significand carry round correctly");
    constexpr std::uint64_t cross = (std::uint64_t{1} << 32U) + std::uint64_t{1};
    require(
        methods::wide_multiply(cross, cross) ==
            methods::WideUnsigned{.high = 1, .low = (std::uint64_t{1} << 33U) + std::uint64_t{1}},
        "independent32bit cross products preserve middle carry");
    require(methods::wide_double(zero) == 0 && methods::wide_double(one) == 1,
            "zero and exact low-limb integers convert without shifts");
    require(methods::wide_double({.high = 0x8000000000000400ULL, .low = 0}) == 0x1p127 &&
                methods::wide_double({.high = 0x8000000000000401ULL, .low = 0}) ==
                    0x1.0000000000001p127 &&
                methods::wide_double({.high = 0x8000000000000c00ULL, .low = 0}) ==
                    0x1.0000000000002p127,
            "high-limb even odd and sticky bits round independently across64bit boundary");
    require(methods::wide_double({.high = 0, .low = 9007199254740991ULL}) ==
                    0x1.fffffffffffffp+52 &&
                methods::wide_double({.high = 0, .low = max}) == 0x1p64 &&
                methods::wide_double({.high = 1, .low = 6144}) == 0x1.0000000000002p64,
            "all original independent conversion literals execute in the shared callback");
    constexpr methods::WideUnsigned equal{.high = 37, .low = 91};
    require(methods::wide_subtract(equal, equal) == zero,
            "equal nonzero two-limb subtraction returns present exact zero");
    require(methods::wide_double(
                {.high = std::uint64_t{1} << 52U, .low = std::uint64_t{1} << 63U}) == 0x1p116 &&
                methods::wide_double({
                    .high = std::uint64_t{1} << 52U,
                    .low = (std::uint64_t{1} << 63U) + 1,
                }) == 0x1.0000000000001p116,
            "exact shift64 tie and low-limb sticky round independently");
    require(methods::wide_double({.high = 0x8000000000000400ULL, .low = 1}) ==
                0x1.0000000000001p127,
            "high-limb guard retains sticky solely in the low limb");
    constexpr std::uint64_t n = 16769025;
    constexpr std::uint64_t sum = 1098958053374;
    constexpr std::uint64_t squares = 72020216027799556;
    const auto variance = methods::wide_subtract(methods::wide_multiply(n, squares),
                                                 methods::wide_multiply(sum, sum));
    require(variance == methods::WideUnsigned{.high = 0, .low = 16769024},
            "actual limb owner preserves independently known N-1 variance after80bit cancellation");
}
} // namespace docenhance::tests
