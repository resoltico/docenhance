// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/host/processor.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "docenhance/methods/fixed_threshold.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>
namespace docenhance::host {
core::Result<app::Processed> Processor::process(const app::ProcessRequest& request) {
    constexpr std::size_t processing_budget_bytes = std::size_t{128} * 1024 * 1024;
    core::Budget budget{processing_budget_bytes};
    auto source = io::load_grayscale_png(request.input(), budget);
    if (!source) {
        return std::unexpected(source.error());
    }
    auto destination =
        image::Plane<std::uint8_t>::allocate(budget, source->width(), source->height());
    if (!destination) {
        return std::unexpected(destination.error());
    }
    const auto applied = methods::fixed_threshold(source->view().as_const(), destination->view(),
                                                  request.threshold());
    if (!applied) {
        return std::unexpected(applied.error());
    }
    auto published = io::publish_grayscale_png(request.output_directory(),
                                               destination->view().as_const(), budget);
    if (!published) {
        return std::unexpected(published.error());
    }
    return app::Processed{.output = std::move(*published)};
}
} // namespace docenhance::host
