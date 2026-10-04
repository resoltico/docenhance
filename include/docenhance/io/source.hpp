// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"

#include <string>
namespace docenhance::io {
struct IdentifiedRaster {
    image::Raster raster;
    core::ContentIdentity source;
    image::SourceDescription description;
};
// One read-only acquisition and identity boundary; signatures choose one decoder, never retries.
[[nodiscard]] core::Result<IdentifiedRaster>
load_source(const std::string& input, core::Budget& budget, image::ProfilePolicy policy,
            const core::Cancellation& cancellation = {});
} // namespace docenhance::io
