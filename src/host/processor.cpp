// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/host/processor.hpp"

#include "continuous.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/illumination.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>
#include <variant>
namespace docenhance::host {
namespace {
core::Result<app::PublishedImage> binary(const app::ProcessRequest& request,
                                         const methods::Binarization& method,
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
    const auto scratch = methods::scratch_bytes(method, source->width(), 1);
    if (!scratch) {
        return std::unexpected(scratch.error());
    }
    const auto concurrency = exec::Concurrency::resolve(std::nullopt, exec::detected_concurrency(),
                                                        budget.available(), *scratch);
    if (!concurrency) {
        return std::unexpected(concurrency.error());
    }
    const exec::Scheduler scheduler{*concurrency, cancellation};
    const auto applied = methods::binarize(source->view().as_const(), destination->view(), method,
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
} // namespace
app::ProcessResult Processor::process(const app::ProcessRequest& request,
                                      const core::Cancellation& cancellation) {
    if (cancellation.requested(core::Checkpoint::admission)) {
        return app::process_failure(core::cancelled().error());
    }
    if (const auto* const method = std::get_if<methods::Binarization>(&request.operation())) {
        auto result = binary(request, *method, cancellation);
        if (!result) {
            return app::process_failure(std::move(result.error()));
        }
        return std::move(*result);
    }
    methods::IlluminationReport report;
    auto result =
        continuous(request, std::get<image::Continuous>(request.operation()), cancellation, report);
    if (!result) {
        if (report.requested && !report.complete) {
            report.status = methods::SurfaceStatus::failed;
            if (report.reason == methods::SurfaceReason::none ||
                report.reason == methods::SurfaceReason::no_effect) {
                report.reason = methods::SurfaceReason::processing_failure;
            }
        }
        return app::process_failure(std::move(result.error()), report);
    }
    return std::move(*result);
}
} // namespace docenhance::host
