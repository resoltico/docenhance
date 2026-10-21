// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source_facts.hpp"

#include <cstddef>
#include <span>
namespace docenhance::codec {
struct DecodedRaster {
    image::Raster raster;
    image::SourceFacts description;
};
[[nodiscard]] core::Result<DecodedRaster> decode_source(std::span<const std::byte> input,
                                                        core::Budget& budget,
                                                        image::ProfilePolicy policy,
                                                        const core::Cancellation& cancellation);
} // namespace docenhance::codec
