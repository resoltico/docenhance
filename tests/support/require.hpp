// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
namespace docenhance::tests {
// The whole test vocabulary: a failed expectation is an exception with the sentence that failed,
// so the same cases run under Catch2 and under the dependency-free reference runner.
inline void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}
} // namespace docenhance::tests
