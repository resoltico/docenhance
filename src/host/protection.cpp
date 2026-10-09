// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "protection.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/color/converter.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/protection_png.hpp"
#include "run_publication.hpp"

#include <cstdint>
#include <expected>
#include <utility>

namespace docenhance::host {
core::Result<Protection> load_protection(const app::ProcessRequest& request,
                                         const color::Converter& converter, core::Budget& budget,
                                         const core::Cancellation& cancellation) {
    Protection result;
    if (request.protection()) {
        const auto report = converter.report();
        const auto shape = image::oriented_shape(report.source, report.orientation);
        auto loaded = io::load_protection_png(*request.protection(),
                                              {.width = shape.width, .height = shape.height},
                                              budget, cancellation);
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
