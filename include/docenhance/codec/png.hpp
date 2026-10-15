// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source_facts.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace docenhance::codec {
// Callers may tighten, never relax, the production input limits. The byte span is borrowed
// for this call only. Memory decoding uses the same CRC/format/sample/allocator path as files.
inline constexpr std::size_t png_max_encoded_bytes = image::source_encoded_bytes_max;
inline constexpr std::uint64_t png_max_pixels = image::source_pixels_max;
struct PngLimits {
    std::size_t encoded_bytes = png_max_encoded_bytes;
    std::uint64_t pixels = png_max_pixels;
};
[[nodiscard]] core::Result<image::Plane<std::uint8_t>>
decode_grayscale_png(std::span<const std::uint8_t> input, core::Budget& budget,
                     PngLimits limits = PngLimits(), const core::Cancellation& cancellation = {});

} // namespace docenhance::codec
