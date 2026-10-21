// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
namespace docenhance::tests {
inline std::atomic<std::uint64_t>& assertion_counter() {
    static std::atomic<std::uint64_t> counter{0};
    return counter;
}
// The whole test vocabulary: a failed expectation is an exception with the sentence that failed,
// so the same cases run under Catch2 and under the dependency-free reference runner.
inline void require(bool condition, std::string_view message) {
    assertion_counter().fetch_add(1, std::memory_order_relaxed);
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}
} // namespace docenhance::tests
