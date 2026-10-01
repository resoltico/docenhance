// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/bundle.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace docenhance::io {
// A decoded image and the identity of the encoded bytes it came from. The identity is taken from
// the immutable snapshot the decoder consumed, so it cannot describe a later state of that path.
struct IdentifiedImage {
    image::Plane<std::uint8_t> image;
    core::ContentIdentity source;
    image::PngSource description;
};
// UTF-8 paths. Stored grayscale samples are expanded to 8 bits without gamma/color transforms.
// Images, libpng and zlib allocations share the supplied byte budget. Metadata and OS resources
// have separate fixed limits; this is not a process-RSS guarantee.
[[nodiscard]] core::Result<IdentifiedImage>
load_grayscale_png(const std::string& input, core::Budget& budget,
                   const core::Cancellation& cancellation = {});

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

// Encode a binary image into an existing directory and read it back against the intended samples
// and metadata. Used by a publication transaction, which owns the directory and the commit.
[[nodiscard]] core::Result<void> write_verified_png(const BundleSlot& slot,
                                                    image::PlaneView<const std::uint8_t> image,
                                                    core::Budget& budget,
                                                    const core::Cancellation& cancellation = {});

} // namespace docenhance::io
