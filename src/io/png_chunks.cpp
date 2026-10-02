// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "png_chunks.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "png_metadata.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <zconf.h>
#include <zlib.h>
namespace docenhance::io {

core::Result<PngChunk> take_png_chunk(std::span<const std::uint8_t>& bytes,
                                      const core::Cancellation& cancellation) {
    constexpr std::size_t crc_bytes = png_integer_bytes;
    constexpr std::size_t transfer_bytes = std::size_t{64} * 1024;
    if (bytes.size() < png_chunk_overhead) {
        return core::failure(core::ErrorCode::input, "Truncated PNG chunk");
    }
    const auto size = png_integer(bytes);
    if (size > bytes.size() - png_chunk_overhead) {
        return core::failure(core::ErrorCode::input, "PNG chunk extent exceeds encoded input");
    }
    const auto name = bytes.subspan(png_integer_bytes, png_integer_bytes);
    const bool letters = std::ranges::all_of(
        name, [](auto c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); });
    if (!letters || name.subspan(2, 1).front() < 'A' || name.subspan(2, 1).front() > 'Z') {
        return core::failure(core::ErrorCode::input, "PNG chunk name is invalid");
    }
    auto protected_bytes = bytes.subspan(png_integer_bytes, std::size_t{size} + crc_bytes);
    uLong crc = crc32(0, Z_NULL, 0);
    while (!protected_bytes.empty()) {
        if (cancellation.requested(core::Checkpoint::decode)) {
            return core::cancelled();
        }
        const auto part = protected_bytes.first(std::min(transfer_bytes, protected_bytes.size()));
        crc = crc32(crc, part.data(), static_cast<uInt>(part.size()));
        protected_bytes = protected_bytes.subspan(part.size());
    }
    const auto data = bytes.subspan(png_signature_bytes, size);
    if (crc != png_integer(bytes.subspan(png_signature_bytes + size))) {
        return core::failure(core::ErrorCode::input, "PNG chunk CRC mismatch");
    }
    const auto type = png_integer(name);
    bytes = bytes.subspan(std::size_t{size} + png_chunk_overhead);
    return PngChunk{.type = type, .data = data};
}
} // namespace docenhance::io
