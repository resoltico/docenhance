// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstddef>

namespace docenhance::core {
// Shared execution and observation domains. Charged buffers are not process RSS.
inline constexpr std::size_t binary_processing_budget = std::size_t{128} * 1024 * 1024;
inline constexpr std::size_t continuous_processing_budget = std::size_t{1024} * 1024 * 1024;
// Every produced artifact must remain in the bundle reader's admitted byte domain.
inline constexpr std::size_t bundle_max_file_bytes = std::size_t{256} * 1024 * 1024;
inline constexpr std::size_t psf_encoded_bytes_max = std::size_t{1024} * 1024;
inline constexpr std::size_t psf_dimension_max = 129;
} // namespace docenhance::core
