// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace docenhance::io {
// The identity of a byte sequence that was actually read or written, taken while those bytes are
// in hand. Identifying a path later answers a different question: the file may have changed.
//
// A digest detects disagreement with expected bytes. It is not a signature, it names no author,
// and two internally consistent artifacts can be produced by anyone able to rewrite both.
struct ContentIdentity {
    std::string sha256; // 64 lowercase hexadecimal characters
    std::uint64_t bytes = 0;
};
// The number of characters a rendered digest always has.
inline constexpr std::size_t sha256_hex_length = 64;

[[nodiscard]] core::Result<ContentIdentity> identify(std::span<const std::byte> content);
} // namespace docenhance::io
