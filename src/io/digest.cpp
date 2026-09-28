// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/io/digest.hpp"

#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <picosha2.h>
#include <span>
#include <string>
#include <utility>

namespace docenhance::io {
core::Result<core::ContentIdentity> identify(std::span<const std::byte> content) {
    // uint8_t is the unsigned-byte view of the same immutable storage.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const std::span bytes{reinterpret_cast<const std::uint8_t*>(content.data()), content.size()};
    try {
        std::string hex;
        picosha2::hash256_hex_string(bytes.begin(), bytes.end(), hex);
        if (hex.size() != core::sha256_hex_length) {
            return core::failure(core::ErrorCode::invariant, "A digest was not fully rendered");
        }
        return core::ContentIdentity{.sha256 = std::move(hex), .bytes = content.size()};
    } catch (const std::bad_alloc&) {
        return core::failure(core::ErrorCode::resource, "Identifying content exhausted memory");
    }
}
} // namespace docenhance::io
