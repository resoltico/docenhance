// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace docenhance::bundle {
// Reading a record that someone else wrote. A supplied bundle is untrusted input: it is bounded
// in size and in nesting before it is parsed at all, it is parsed without exceptions, and what
// comes back is typed. No field can name a path outside the bundle, and nothing here executes,
// reruns or interprets the request that was recorded.

// A record larger or deeper than this is refused rather than survived. The depth bound is checked
// before parsing, because a recursive-descent parser meets a deeply nested document with the
// stack, not with an error.
inline constexpr std::size_t record_max_bytes = std::size_t{1} * 1024 * 1024;
inline constexpr std::size_t record_max_depth = 16;
inline constexpr std::size_t record_max_artifacts = 16;

// What a record declares this bundle contains. The inventory is closed: a file that is present
// and undeclared is as much a disagreement as a declared file that is missing.
struct DeclaredBundle {
    unsigned version = 0;
    std::string run;
    std::string recorded;
    std::vector<Artifact> inventory;
};

[[nodiscard]] core::Result<DeclaredBundle> read_record(std::span<const std::byte> bytes);
} // namespace docenhance::bundle
