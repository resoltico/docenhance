// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/host/processor.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "docenhance/methods/binarization.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>
namespace docenhance::host {
core::Result<app::PublishedImage> Processor::process(const app::ProcessRequest& request,
                                                     const core::Cancellation& cancellation) {
    if (cancellation.requested(core::Checkpoint::admission)) {
        return core::cancelled();
    }
    constexpr std::size_t processing_budget_bytes = std::size_t{128} * 1024 * 1024;
    core::Budget budget{processing_budget_bytes};
    auto source = io::load_grayscale_png(request.input(), budget, cancellation);
    if (!source) {
        return std::unexpected(source.error());
    }
    if (cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto destination =
        image::Plane<std::uint8_t>::allocate(budget, source->width(), source->height());
    if (!destination) {
        return std::unexpected(destination.error());
    }
    const auto scratch = methods::scratch_bytes(request.method(), source->width(), 1);
    if (!scratch) {
        return std::unexpected(scratch.error());
    }
    const auto concurrency = exec::Concurrency::resolve(std::nullopt, exec::detected_concurrency(),
                                                        budget.available(), *scratch);
    if (!concurrency) {
        return std::unexpected(concurrency.error());
    }
    const exec::Scheduler scheduler{*concurrency, cancellation};
    const auto applied =
        methods::binarize(source->view().as_const(), destination->view(), request.method(),
                          {.scheduler = scheduler, .budget = budget});
    if (!applied) {
        return std::unexpected(applied.error());
    }
    auto published = io::publish_grayscale_png(
        request.output_directory(), destination->view().as_const(), budget, cancellation);
    if (!published) {
        return std::unexpected(published.error());
    }
    return app::PublishedImage{.output = std::move(*published)};
}
} // namespace docenhance::host
