// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <optional>
namespace docenhance::methods {
struct WideUnsigned {
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    bool operator==(const WideUnsigned&) const = default;
};
[[nodiscard]] WideUnsigned wide_multiply(std::uint64_t left, std::uint64_t right) noexcept;
[[nodiscard]] std::optional<WideUnsigned> wide_subtract(WideUnsigned left,
                                                        WideUnsigned right) noexcept;
[[nodiscard]] double wide_double(WideUnsigned value) noexcept;
} // namespace docenhance::methods
