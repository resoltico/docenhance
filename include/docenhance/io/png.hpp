// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace docenhance::io {
// UTF-8 paths. Stored grayscale samples are expanded to 8 bits without gamma/color transforms.
// Images, libpng and zlib allocations share the supplied byte budget. Metadata and OS resources
// have separate fixed limits; this is not a process-RSS guarantee.
[[nodiscard]] core::Result<image::Plane<std::uint8_t>> load_grayscale_png(const std::string& input,
                                                                          core::Budget& budget);

// Callers may tighten, never relax, the production input limits. The byte span is borrowed
// for this call only. Memory decoding uses the same CRC/format/sample/allocator path as files.
inline constexpr std::size_t png_max_encoded_bytes = std::size_t{128} * 1024 * 1024;
inline constexpr std::uint64_t png_max_pixels = 40'000'000;
struct PngLimits {
    std::size_t encoded_bytes = png_max_encoded_bytes;
    std::uint64_t pixels = png_max_pixels;
};
[[nodiscard]] core::Result<image::Plane<std::uint8_t>>
decode_grayscale_png(std::span<const std::uint8_t> input, core::Budget& budget,
                     PngLimits limits = {});

// Publish result.png in a new directory. A native no-replace rename is the commit point; no
// fallback may overwrite an existing file, directory or symlink. The parent must be trusted
// against hostile ancestor replacement. Atomic visibility does not promise crash durability.
[[nodiscard]] core::Result<std::string>
publish_grayscale_png(const std::string& output_directory,
                      image::PlaneView<const std::uint8_t> image, core::Budget& budget);
} // namespace docenhance::io
