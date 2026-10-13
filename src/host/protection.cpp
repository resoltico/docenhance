// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "protection.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/color/converter.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/protection_png.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/progress.hpp"
#include "run_publication.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <utility>

namespace docenhance::host {
core::Result<void> disabled_protection_observations(image::PlaneView<const std::uint8_t> protection,
                                                    image::Extent extent,
                                                    const core::Cancellation& cancellation,
                                                    methods::IlluminationReport& report) {
    report = {};
    report.progress = methods::StageProgress::preparing;
    for (std::uint32_t y = 0; y < protection.height(); ++y) {
        const auto row = protection.row(y);
        for (std::uint32_t first = 0; first < protection.width();) {
            if (cancellation.requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            const auto count = std::min(image::linear_block_pixels, protection.width() - first);
            report.protected_samples += static_cast<std::uint64_t>(std::ranges::count_if(
                row.subspan(first, count), [](auto value) { return value != 0; }));
            first += count;
        }
    }
    report.eligible_samples =
        (std::uint64_t{extent.width} * extent.height) - report.protected_samples;
    report.progress = methods::StageProgress::completed;
    return {};
}
core::Result<Protection> load_protection(const app::ProcessRequest& request,
                                         const color::Converter& converter, core::Budget& budget,
                                         const core::Cancellation& cancellation) {
    Protection result;
    const auto& mask_path = std::get<app::ContinuousPlan>(request.plan()).protection;
    if (mask_path) {
        const auto report = converter.report();
        const auto shape = image::oriented_shape(report.source, report.orientation);
        auto loaded = io::load_protection_png(
            *mask_path, {.width = shape.width, .height = shape.height}, budget, cancellation);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        result.supplied = std::move(loaded->mask);
        const auto extent = converter.extent();
        if (request.rotation().degrees() != 0) {
            auto rotated =
                image::Plane<std::uint8_t>::allocate(budget, extent.width, extent.height);
            if (!rotated) {
                return std::unexpected(rotated.error());
            }
            auto applied = image::rotate_plane(result.supplied.view().as_const(), rotated->view(),
                                               request.rotation(), cancellation);
            if (!applied) {
                return std::unexpected(applied.error());
            }
            result.mask = std::move(*rotated);
        }
        result.facts = MaskFacts{
            .supplied = std::move(loaded->source),
            .canonical = result.supplied.view().as_const(),
        };
        if (request.rotation().degrees() == 0) {
            result.mask = std::move(result.supplied);
        }
    }
    return result;
}
} // namespace docenhance::host
