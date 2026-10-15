// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source_facts.hpp"

#include <string>
namespace docenhance::fs {
struct BundleSlot;
struct IdentifiedImage {
    image::Plane<std::uint8_t> image;
    core::ContentIdentity source;
    image::PngFacts description;
};
[[nodiscard]] core::Result<IdentifiedImage>
load_grayscale_png(const std::string& input, core::Budget& budget,
                   const core::Cancellation& cancellation = {});
[[nodiscard]] core::Result<void> write_verified_png(const BundleSlot& slot,
                                                    image::PlaneView<const std::uint8_t> image,
                                                    core::Budget& budget,
                                                    const core::Cancellation& cancellation = {});
[[nodiscard]] core::Result<void>
write_verified_png_rows(const BundleSlot& slot, image::RowSource& rows, core::Budget& budget,
                        const core::Cancellation& cancellation = {});
} // namespace docenhance::fs
