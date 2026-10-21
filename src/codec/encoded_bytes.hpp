// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace docenhance::codec {
// Borrow encoded object representation only; the immutable byte owner remains in the caller.
[[nodiscard]] inline std::span<const std::uint8_t>
encoded_bytes(std::span<const std::byte> bytes) noexcept {
    static_assert(std::is_same_v<std::uint8_t, unsigned char>);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()};
}
} // namespace docenhance::codec
