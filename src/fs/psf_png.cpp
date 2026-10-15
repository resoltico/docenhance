// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/psf_png.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/fs/psf_png.hpp"
#include "source_snapshot.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <utility>
namespace docenhance::fs {
core::Result<IdentifiedPsf> load_psf_png(const std::string& path, core::Budget& budget,
                                         const core::Cancellation& cancellation) {
    auto bytes = read_source_snapshot(path, budget, cancellation, codec::psf_encoded_bytes_max);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto identity = identify(bytes->bytes(), cancellation);
    if (!identity) {
        return std::unexpected(identity.error());
    }
    // uint8_t observes the immutable snapshot's object representation; its Buffer stays live.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* const data = reinterpret_cast<const std::uint8_t*>(bytes->bytes().data());
    auto decoded = codec::decode_psf_png({data, bytes->size()}, budget, cancellation);
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    return IdentifiedPsf{.raster = std::move(*decoded), .source = std::move(*identity)};
}
} // namespace docenhance::fs
