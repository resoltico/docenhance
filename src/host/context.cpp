// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/record.hpp"
#include "docenhance/host/processor.hpp"
#include "instant.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>

namespace docenhance::host {
namespace {
constexpr std::size_t identity_words = 2;
constexpr std::size_t nibbles_per_word = 16;
constexpr unsigned word_bits = 32;
constexpr unsigned nibble_bits = 4;
constexpr std::uint64_t nibble_mask = 0xF;
constexpr std::string_view hexadecimal = "0123456789abcdef";

// 128 bits, rendered as hexadecimal. The identity exists before the commit point, which is its
// purpose: after an ambiguous commit the destination can be read and matched against this run.
std::string observed_identity() {
    std::random_device source;
    std::string identity;
    for (std::size_t word = 0; word < identity_words; ++word) {
        const auto high = static_cast<std::uint64_t>(source());
        const auto low = static_cast<std::uint64_t>(source());
        auto value = (high << word_bits) | low;
        const auto start = identity.size();
        identity.append(nibbles_per_word, hexadecimal.front());
        for (auto position = identity.size(); position > start; --position) {
            identity.at(position - 1) = hexadecimal.at(value & nibble_mask);
            value >>= nibble_bits;
        }
    }
    return identity;
}
} // namespace

bundle::RunContext observed_context() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return {
        .identity = observed_identity(),
        .recorded = rfc3339_utc(now.time_since_epoch().count()),
    };
}
} // namespace docenhance::host
