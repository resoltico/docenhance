// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string_view>
namespace docenhance::fuzz {
// A violated property is a finding. Report it and abort, so that every engine (libFuzzer, AFL++,
// the replay driver) records the input as a crash with this message.
[[noreturn]] inline void fail(std::string_view property) noexcept {
    constexpr std::string_view prefix = "fuzz property violated: ";
    // Best effort: the abort below is the finding, whether or not the message is written.
    static_cast<void>(std::fwrite(prefix.data(), 1, prefix.size(), stderr));
    static_cast<void>(std::fwrite(property.data(), 1, property.size(), stderr));
    static_cast<void>(std::fputc('\n', stderr));
    std::abort();
}
inline void require(bool condition, std::string_view property) noexcept {
    if (!condition) {
        fail(property);
    }
}
} // namespace docenhance::fuzz
