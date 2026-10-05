// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstddef>

namespace docenhance::io {
// Closed bundle admission bounds. Result PNG decoding and publication use this same artifact
// byte ceiling, so a produced file remains within the bundle reader's admitted domain.
inline constexpr std::size_t bundle_max_entries = 64;
inline constexpr std::size_t bundle_max_file_bytes = std::size_t{256} * 1024 * 1024;
// Charged snapshot buffers, independently of processing buffers and uncharged OS/metadata storage.
inline constexpr std::size_t bundle_snapshot_budget = std::size_t{1} * 1024 * 1024 * 1024;
} // namespace docenhance::io
