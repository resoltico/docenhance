// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/bundle/record.hpp"
#include "docenhance/host/processor.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <random>
#include <string>

namespace docenhance::host {
namespace {
constexpr std::size_t identity_words = 2;
constexpr int hex_width = 16;
constexpr unsigned word_bits = 32;

// 128 bits, rendered as hexadecimal. The identity exists before the commit point, which is its
// purpose: after an ambiguous commit the destination can be read and matched against this run.
std::string observed_identity() {
    std::random_device source;
    std::string identity;
    for (std::size_t word = 0; word < identity_words; ++word) {
        const auto high = static_cast<std::uint64_t>(source());
        const auto low = static_cast<std::uint64_t>(source());
        identity += std::format("{:0{}x}", (high << word_bits) | low, hex_width);
    }
    return identity;
}
} // namespace

bundle::RunContext observed_context() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return {
        .identity = observed_identity(),
        .recorded = std::format("{:%Y-%m-%dT%H:%M:%SZ}", now),
    };
}
} // namespace docenhance::host
