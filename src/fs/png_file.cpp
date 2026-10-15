// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/source.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/fs/png.hpp"
#include "source_snapshot.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
namespace docenhance::fs {
core::Result<IdentifiedImage> load_grayscale_png(const std::string& input, core::Budget& budget,
                                                 const core::Cancellation& cancellation) {
    // Snapshot first: an identity must describe the bytes this decode consumed. Streaming the
    // file and hashing a later reopening of the same path can answer about two different files.
    auto encoded = read_source_snapshot(input, budget, cancellation);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }
    auto source = identify(encoded->bytes(), cancellation);
    if (!source) {
        return std::unexpected(source.error());
    }
    // uint8_t is the unsigned-byte view of the immutable encoded snapshot.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* const data = reinterpret_cast<const std::uint8_t*>(encoded->bytes().data());
    auto decoded = codec::decode_binary_source({data, encoded->size()}, budget, cancellation);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    return IdentifiedImage{
        .image = std::move(decoded->image),
        .source = std::move(*source),
        .description = decoded->description,
    };
}
} // namespace docenhance::fs
