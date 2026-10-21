// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <cstdint>

namespace docenhance::core {
// Shared encoded input ceiling; each filesystem snapshot caller supplies its role's tighter bound.
inline constexpr std::uint64_t source_encoded_bytes_max = std::uint64_t{128} * 1024 * 1024;
// Every produced artifact must remain in the bundle reader's admitted byte domain.
inline constexpr std::size_t bundle_max_file_bytes = std::size_t{256} * 1024 * 1024;
inline constexpr std::size_t psf_encoded_bytes_max = std::size_t{1024} * 1024;
inline constexpr std::size_t psf_dimension_max = 129;
} // namespace docenhance::core
