// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/digest.hpp"
#include "docenhance/io/png.hpp"
#include "png_snapshot.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <utility>
namespace docenhance::io {
core::Result<IdentifiedImage> load_grayscale_png(const std::string& input, core::Budget& budget,
                                                 const core::Cancellation& cancellation) {
    // Snapshot first: an identity must describe the bytes this decode consumed. Streaming the
    // file and hashing a later reopening of the same path can answer about two different files.
    auto encoded = read_png_snapshot(input, budget, cancellation);
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
    auto decoded = decode_grayscale_png({data, encoded->size()}, budget, PngLimits(), cancellation);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    return IdentifiedImage{.image = std::move(*decoded), .source = std::move(*source)};
}
} // namespace docenhance::io
