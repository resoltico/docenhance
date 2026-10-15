// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/protection_png.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/fs/protection_png.hpp"
#include "docenhance/image/linear.hpp"
#include "source_snapshot.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <utility>
namespace docenhance::fs {
core::Result<IdentifiedMask> load_protection_png(const std::string& path, image::Extent extent,
                                                 core::Budget& budget,
                                                 const core::Cancellation& cancellation) {
    auto bytes = read_source_snapshot(path, budget, cancellation);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto source = identify(bytes->bytes(), cancellation);
    if (!source) {
        return std::unexpected(source.error());
    }
    // Observe immutable encoded bytes; the backing Buffer owns the lifetime.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes->bytes().data());
    auto mask = codec::decode_protection_png({data, bytes->size()}, extent, budget, cancellation);
    if (!mask) {
        return std::unexpected(mask.error());
    }
    return IdentifiedMask{.mask = std::move(*mask), .source = std::move(*source)};
}
} // namespace docenhance::fs
