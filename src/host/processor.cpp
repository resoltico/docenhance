// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/host/processor.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/color/converter.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "docenhance/io/png.hpp"
#include "docenhance/methods/binarization.hpp"

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
core::Result<app::PublishedImage> continuous(const app::ProcessRequest& request,
                                             image::Continuous operation,
                                             const core::Cancellation& cancellation) {
    constexpr std::size_t continuous_budget_bytes = std::size_t{1024} * 1024 * 1024;
    core::Budget budget{continuous_budget_bytes};
    auto source =
        io::load_png_raster(request.input(), budget, operation.parameters().profile, cancellation);
    if (!source) {
        return std::unexpected(source.error());
    }
    auto converter = color::Converter::create(*source, operation, budget, cancellation);
    if (!converter) {
        return std::unexpected(converter.error());
    }
    auto published =
        io::publish_png_rows(request.output_directory(), **converter, budget, cancellation);
    if (!published) {
        return std::unexpected(published.error());
    }
    auto report = (*converter)->report();
    report.verified = true;
    return app::PublishedImage{.output = std::move(*published), .conversion = report};
}
} // namespace
core::Result<app::PublishedImage> Processor::process(const app::ProcessRequest& request,
                                                     const core::Cancellation& cancellation) {
    if (cancellation.requested(core::Checkpoint::admission)) {
        return core::cancelled();
    }
    if (const auto* const method = std::get_if<methods::Binarization>(&request.operation())) {
        return binary(request, *method, cancellation);
    }
    return continuous(request, std::get<image::Continuous>(request.operation()), cancellation);
}
} // namespace docenhance::host
