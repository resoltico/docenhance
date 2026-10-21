// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/limits.hpp"
#include "docenhance/image/source_facts.hpp"

#include <cstddef>
#include <cstdint>

namespace docenhance::codec {
// Callers may tighten, never relax, the production input limits.
inline constexpr std::size_t png_max_encoded_bytes = core::source_encoded_bytes_max;
inline constexpr std::uint64_t png_max_pixels = image::source_pixels_max;
struct PngLimits {
    std::size_t encoded_bytes = png_max_encoded_bytes;
    std::uint64_t pixels = png_max_pixels;
};
} // namespace docenhance::codec
