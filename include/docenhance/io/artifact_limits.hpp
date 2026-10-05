// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstddef>

namespace docenhance::io {
// Native inventory and snapshot admission bounds, independent of processing buffers.
inline constexpr std::size_t bundle_max_entries = 64;
// Charged snapshot buffers, independently of processing buffers and uncharged OS/metadata storage.
inline constexpr std::size_t bundle_snapshot_budget = std::size_t{1} * 1024 * 1024 * 1024;
} // namespace docenhance::io
