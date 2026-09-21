// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
namespace docenhance::fuzz {
// Deterministic consumer of fuzz input bytes. Unlike libFuzzer's FuzzedDataProvider it compiles
// with every supported compiler, so the same harnesses replay their corpora in normal builds.
// Exhausted input reads as zero bytes.
class FuzzInput final {
  public:
    explicit FuzzInput(std::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}
    [[nodiscard]] bool empty() const noexcept {
        return bytes_.empty();
    }
    [[nodiscard]] std::uint8_t byte() noexcept {
        if (bytes_.empty()) {
            return 0;
        }
        const std::uint8_t value = bytes_.front();
        bytes_ = bytes_.subspan(1);
        return value;
    }
    template <std::unsigned_integral T> [[nodiscard]] T integer() noexcept {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            value = (value << 8U) | byte();
        }
        return static_cast<T>(value);
    }
    // Uniform-ish in [0, max], inclusive.
    [[nodiscard]] std::uint64_t bounded(std::uint64_t max) noexcept {
        const auto raw = integer<std::uint64_t>();
        return max == std::numeric_limits<std::uint64_t>::max() ? raw : raw % (max + 1);
    }
    // Any bit pattern, including NaN, infinities, subnormals and negative zero.
    [[nodiscard]] double any_double() noexcept {
        return std::bit_cast<double>(integer<std::uint64_t>());
    }
    // In [0, 1], both endpoints reachable.
    [[nodiscard]] double unit() noexcept {
        constexpr std::uint64_t mantissa_max = (std::uint64_t{1} << 53U) - 1;
        return static_cast<double>(integer<std::uint64_t>() & mantissa_max) /
               static_cast<double>(mantissa_max);
    }
    [[nodiscard]] std::string string(std::size_t max_length) {
        const auto length = static_cast<std::size_t>(bounded(max_length));
        return take(length);
    }
    [[nodiscard]] std::string rest(std::size_t max_length) {
        return take(max_length);
    }

  private:
    [[nodiscard]] std::string take(std::size_t length) {
        const auto taken = bytes_.first(std::min(length, bytes_.size()));
        bytes_ = bytes_.subspan(taken.size());
        // Explicit byte-to-char conversion: the strict sanitizer rejects implicit sign changes.
        std::string text(taken.size(), '\0');
        std::ranges::transform(taken, text.begin(),
                               [](std::uint8_t byte) { return static_cast<char>(byte); });
        return text;
    }
    std::span<const std::uint8_t> bytes_;
};
} // namespace docenhance::fuzz
