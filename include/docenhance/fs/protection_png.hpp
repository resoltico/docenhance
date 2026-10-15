// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/codec/protection_png.hpp"
#include "docenhance/core/identity.hpp"

#include <string>
namespace docenhance::fs {
// The mask as supplied, and the identity of the bytes that were read. The canonical plane the
// decoder produces is what excluded samples; the identity describes the file it came from.
struct IdentifiedMask {
    image::Plane<std::uint8_t> mask;
    core::ContentIdentity source;
};
[[nodiscard]] core::Result<IdentifiedMask>
load_protection_png(const std::string& path, image::Extent extent, core::Budget& budget,
                    const core::Cancellation& cancellation = {});
} // namespace docenhance::fs
