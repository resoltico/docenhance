// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "png_metadata.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/png.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#include <zconf.h>
#include <zlib.h>

namespace docenhance::io {
namespace {
constexpr auto signature = std::to_array<std::uint8_t>({137, 80, 78, 71, 13, 10, 26, 10});
constexpr std::uint32_t chunk_iccp = 0x69434350;
constexpr std::uint32_t chunk_srgb = 0x73524742;
constexpr std::uint32_t chunk_gama = 0x67414d41;
constexpr std::uint32_t chunk_chrm = 0x6348524d;
constexpr std::uint32_t chunk_cicp = 0x63494350;
constexpr std::uint32_t chunk_exif = 0x65584966;
constexpr std::uint32_t chunk_phys = 0x70485973;
constexpr std::uint32_t chunk_actl = 0x6163544c;
constexpr std::uint32_t chunk_fctl = 0x6663544c;
constexpr std::uint32_t chunk_fdat = 0x66644154;
constexpr std::uint32_t ancillary_bit = 0x20000000;
constexpr std::size_t crc_bytes = png_integer_bytes;
constexpr std::size_t transfer_bytes = std::size_t{64} * 1024;
constexpr std::size_t max_chunks = 65536;
struct Chunk {
    std::uint32_t type{};
    std::span<const std::uint8_t> data;
};
core::Result<Chunk> take_chunk(std::span<const std::uint8_t>& bytes,
                               const core::Cancellation& cancellation) {
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
    return Chunk{.type = type, .data = data};
}
struct ScanState {
    PngScan scan;
    std::uint64_t pixel_limit{};
    unsigned seen{};
    bool header = false;
    bool palette = false;
    bool data = false;
    bool after_data = false;
    std::span<const std::uint8_t> exif;
};
core::Result<void> header(ScanState& state, const Chunk& chunk) {
    constexpr std::size_t header_size = 13;
    if (state.header || chunk.data.size() != header_size) {
        return core::failure(core::ErrorCode::input, "PNG must have one valid IHDR");
    }
    state.header = true;
    state.scan.shape.width = png_integer(chunk.data);
    state.scan.shape.height = png_integer(chunk.data.subspan(png_integer_bytes));
    const auto pixels = std::uint64_t{state.scan.shape.width} * state.scan.shape.height;
    if (pixels == 0 || pixels > state.pixel_limit) {
        return core::failure(core::ErrorCode::resource, "PNG exceeds the admitted pixel ceiling");
    }
    return {};
}
core::Result<void> ordering(ScanState& state, const Chunk& chunk) {
    if (chunk.type == chunk_ihdr) {
        return header(state, chunk);
    }
    if (!state.header) {
        return core::failure(core::ErrorCode::input, "PNG IHDR must be first");
    }
    if (chunk.type == chunk_idat) {
        if (state.after_data) {
            return core::failure(core::ErrorCode::input, "PNG IDAT chunks must be consecutive");
        }
        state.data = true;
    } else if (state.data) {
        state.after_data = true;
    }
    if (chunk.type == chunk_actl || chunk.type == chunk_fctl || chunk.type == chunk_fdat) {
        return core::failure(core::ErrorCode::input, "Animated PNG is not supported");
    }
    if ((chunk.type & ancillary_bit) == 0 && !pixel_chunk(chunk.type)) {
        return core::failure(core::ErrorCode::input, "Unknown critical PNG chunk");
    }
    if ((chunk.type == chunk_plte || chunk.type == chunk_trns) && state.data) {
        return core::failure(core::ErrorCode::input, "PNG palette/transparency follows image data");
    }
    if (chunk.type == chunk_plte) {
        state.palette = true;
    }
    return {};
}
unsigned declaration_bit(std::uint32_t type) noexcept {
    enum class Declaration : unsigned {
        icc = 1,
        srgb = 2,
        gamma = 4,
        chroma = 8,
        cicp = 16,
        exif = 32,
        phys = 64,
    };
    switch (type) {
    case chunk_iccp:
        return static_cast<unsigned>(Declaration::icc);
    case chunk_srgb:
        return static_cast<unsigned>(Declaration::srgb);
    case chunk_gama:
        return static_cast<unsigned>(Declaration::gamma);
    case chunk_chrm:
        return static_cast<unsigned>(Declaration::chroma);
    case chunk_cicp:
        return static_cast<unsigned>(Declaration::cicp);
    case chunk_exif:
        return static_cast<unsigned>(Declaration::exif);
    case chunk_phys:
        return static_cast<unsigned>(Declaration::phys);
    default:
        return 0;
    }
}
core::Result<void> color_declaration(image::PngMetadata& meta, const Chunk& chunk,
                                     core::Budget& budget) {
    constexpr std::size_t chroma_bytes = 32;
    if (chunk.type == chunk_iccp) {
        auto profile = inflate_profile(chunk.data, budget);
        if (!profile) {
            return std::unexpected(profile.error());
        }
        meta.icc = std::move(*profile);
    } else if (chunk.type == chunk_srgb && chunk.data.size() == 1) {
        constexpr unsigned last_intent = 3;
        if (chunk.data.front() > last_intent) {
            return core::failure(core::ErrorCode::input, "PNG sRGB intent is invalid");
        }
        meta.srgb = chunk.data.front();
    } else if (chunk.type == chunk_gama && chunk.data.size() == png_integer_bytes &&
               png_integer(chunk.data) != 0) {
        meta.gamma = png_integer(chunk.data);
    } else if (chunk.type == chunk_chrm && chunk.data.size() == chroma_bytes) {
        std::array<std::uint32_t, image::chromaticity_fields> values{};
        for (std::size_t i = 0; i < values.size(); ++i) {
            values.at(i) = png_integer(chunk.data.subspan(png_integer_bytes * i));
        }
        meta.chromaticities = values;
    } else if (chunk.type == chunk_cicp && chunk.data.size() == png_integer_bytes) {
        std::array<std::uint8_t, image::cicp_fields> values{};
        std::ranges::copy(chunk.data, values.begin());
        meta.cicp = values;
    } else {
        return core::failure(core::ErrorCode::input, "Malformed PNG color declaration");
    }
    return {};
}
core::Result<void> declaration(ScanState& state, const Chunk& chunk, core::Budget& budget,
                               image::ProfilePolicy policy) {
    const auto bit = declaration_bit(chunk.type);
    if (bit == 0) {
        return {};
    }
    if ((state.seen & bit) != 0) {
        return core::failure(core::ErrorCode::input, "Duplicate PNG metadata declaration");
    }
    state.seen |= bit;
    if (chunk.type == chunk_exif) {
        state.exif = chunk.data;
        return {};
    }
    if (state.data || (state.palette && chunk.type != chunk_phys)) {
        return core::failure(core::ErrorCode::input, "PNG metadata is out of order");
    }
    if (chunk.type == chunk_phys) {
        constexpr std::size_t phys_bytes = 9;
        if (chunk.data.size() != phys_bytes || chunk.data.back() > 1) {
            return core::failure(core::ErrorCode::input, "Malformed PNG physical resolution");
        }
        if (chunk.data.back() == 1) {
            const auto x = png_integer(chunk.data);
            const auto y = png_integer(chunk.data.subspan(png_integer_bytes));
            if (x == 0 || y == 0) {
                return core::failure(core::ErrorCode::input, "Zero physical resolution");
            }
            state.scan.metadata.resolution = image::Resolution{.x = x, .y = y};
        }
        return {};
    }
    if (chunk.data.size() > image::profile_limit) {
        return core::failure(core::ErrorCode::resource,
                             "PNG color metadata exceeds its byte ceiling");
    }
    return policy == image::ProfilePolicy::srgb
               ? core::Result<void>{}
               : color_declaration(state.scan.metadata, chunk, budget);
}
core::Result<void> finish(ScanState& state, const Chunk& chunk) {
    if (!state.data || !chunk.data.empty()) {
        return core::failure(core::ErrorCode::input, "PNG has no image data or an invalid IEND");
    }
    if (!state.scan.metadata.icc.empty() && state.scan.metadata.srgb) {
        return core::failure(core::ErrorCode::input, "PNG cannot declare both iCCP and sRGB");
    }
    return (state.seen & declaration_bit(chunk_exif)) == 0
               ? core::Result<void>{}
               : parse_exif(state.exif, state.scan.metadata);
}
} // namespace
std::uint32_t png_integer(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t result = 0;
    for (const auto value : bytes.first(png_integer_bytes)) {
        result = (result << image::byte_bits) | value;
    }
    return result;
}
bool pixel_chunk(std::uint32_t type) noexcept {
    return type == chunk_ihdr || type == chunk_plte || type == chunk_idat || type == chunk_iend ||
           type == chunk_trns;
}
core::Result<PngScan> scan_png(std::span<const std::uint8_t> bytes, core::Budget& budget,
                               image::ProfilePolicy policy, const core::Cancellation& cancellation,
                               PngLimits limits) {
    if (bytes.size() > std::min(png_max_encoded_bytes, limits.encoded_bytes)) {
        return core::failure(core::ErrorCode::resource, "PNG exceeds the encoded byte ceiling");
    }
    if (bytes.size() < signature.size() ||
        !std::ranges::equal(signature, bytes.first(signature.size()))) {
        return core::failure(core::ErrorCode::input, "Invalid PNG signature");
    }
    bytes = bytes.subspan(signature.size());
    ScanState state;
    state.pixel_limit = std::min(png_max_pixels, limits.pixels);
    for (std::size_t count = 0; count < max_chunks && !bytes.empty(); ++count) {
        auto chunk = take_chunk(bytes, cancellation);
        if (!chunk) {
            return std::unexpected(chunk.error());
        }
        auto order = ordering(state, *chunk);
        if (!order) {
            return std::unexpected(order.error());
        }
        auto meta = declaration(state, *chunk, budget, policy);
        if (!meta) {
            return std::unexpected(meta.error());
        }
        if (chunk->type == chunk_iend) {
            auto end = finish(state, *chunk);
            if (!end) {
                return std::unexpected(end.error());
            }
            if (!bytes.empty()) {
                return core::failure(core::ErrorCode::input, "Bytes follow PNG IEND");
            }
            return std::move(state.scan);
        }
    }
    return core::failure(core::ErrorCode::input,
                         "Missing PNG IEND or chunk count ceiling exceeded");
}
} // namespace docenhance::io
