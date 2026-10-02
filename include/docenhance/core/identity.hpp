// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace docenhance::core {
// The identities this project records. Both are vocabulary: the layers that compute them, carry
// them and write them down must all name the same type, or a record and its verifier can disagree
// about what was being identified.

// The identity of a byte sequence that was actually read or written, taken while those bytes are
// in hand. Identifying a path later answers a different question: the file may have changed.
//
// A digest detects disagreement with expected bytes. It is not a signature, it names no author,
// and two internally consistent artifacts can be produced by anyone able to rewrite both.
struct ContentIdentity {
    std::string sha256; // 64 lowercase hexadecimal characters
    std::uint64_t bytes = 0;
};
// A file named relative to a directory, and the identity of what it holds.
struct NamedContent {
    std::string name;
    ContentIdentity identity;
};
// The number of characters a rendered digest always has.
inline constexpr std::size_t sha256_hex_length = 64;

inline constexpr std::size_t run_identity_hex_length = 32;
[[nodiscard]] bool valid_hexadecimal(std::string_view value, std::size_t length);
[[nodiscard]] bool valid_instant(std::string_view value);

// The identity of this build, as the build system recorded it.
struct BuildFacts {
    std::string_view version;
    std::string_view platform;
    std::string_view compiler;
    std::string_view dependency_lock_sha256;
};
// What the build wrote down about itself. One reader, so a response and a record cannot describe
// two different builds.
[[nodiscard]] BuildFacts build_facts() noexcept;
} // namespace docenhance::core
