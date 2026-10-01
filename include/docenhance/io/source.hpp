// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/io/continuous_png.hpp"

#include <string>
namespace docenhance::io {
// One read-only acquisition and identity boundary; signatures choose one decoder, never retries.
[[nodiscard]] core::Result<IdentifiedRaster>
load_source(const std::string& input, core::Budget& budget, image::ProfilePolicy policy,
            const core::Cancellation& cancellation = {});
} // namespace docenhance::io
