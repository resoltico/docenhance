// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/color/converter.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "run_publication.hpp"

#include <cstdint>
#include <optional>

namespace docenhance::host {
struct Protection {
    image::Plane<std::uint8_t> mask;
    image::Plane<std::uint8_t> supplied;
    std::optional<MaskFacts> facts;
};
[[nodiscard]] core::Result<Protection> load_protection(const app::ProcessRequest& request,
                                                       const color::Converter& converter,
                                                       core::Budget& budget,
                                                       const core::Cancellation& cancellation);
} // namespace docenhance::host
