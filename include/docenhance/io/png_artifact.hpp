// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace docenhance::io {
struct PngArtifact {
    image::RasterShape shape;
    bool profile_embedded = false;
    core::Buffer profile;
    std::optional<image::Resolution> resolution;
    bool binary_samples = true;
    bool mask_samples = true;
    std::uint64_t protected_samples = 0;
};
[[nodiscard]] core::Result<PngArtifact>
observe_png_artifact(std::span<const std::byte> bytes, core::Budget& budget,
                     const core::Cancellation& cancellation);
} // namespace docenhance::io
