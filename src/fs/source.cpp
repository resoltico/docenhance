// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/fs/source.hpp"

#include "docenhance/codec/source.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/image/continuous.hpp"
#include "source_snapshot.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <utility>
namespace docenhance::fs {
core::Result<IdentifiedRaster> load_source(const std::string& input, core::Budget& budget,
                                           image::ProfilePolicy policy,
                                           const core::Cancellation& cancellation) {
    auto encoded = read_source_snapshot(input, budget, cancellation);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }
    auto identity = identify(encoded->bytes(), cancellation);
    if (!identity) {
        return std::unexpected(identity.error());
    }
    // The immutable encoded Buffer owns the bytes throughout decoding.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* const data = reinterpret_cast<const std::uint8_t*>(encoded->bytes().data());
    auto decoded = codec::decode_source({data, encoded->size()}, budget, policy, cancellation);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    return IdentifiedRaster{
        .raster = std::move(decoded->raster),
        .source = std::move(*identity),
        .description = decoded->description,
    };
}
} // namespace docenhance::fs
