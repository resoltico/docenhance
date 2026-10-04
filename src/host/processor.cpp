// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/host/processor.hpp"

#include "bundle.hpp"
#include "continuous.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/bundle.hpp"
#include "docenhance/io/png.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>
#include <variant>
namespace docenhance::host {
namespace {
core::Result<app::PublishedBinary> binary(const app::ProcessRequest& request,
                                          const methods::Binarization& method,
                                          const core::Cancellation& cancellation,
                                          const bundle::RunContext& context) {
    if (cancellation.requested(core::Checkpoint::admission)) {
        return core::cancelled();
    }
    constexpr std::size_t processing_budget_bytes = std::size_t{128} * 1024 * 1024;
    core::Budget budget{processing_budget_bytes};
    auto loaded = io::load_grayscale_png(request.input(), budget, cancellation);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    auto& source = loaded->image;
    if (cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto destination =
        image::Plane<std::uint8_t>::allocate(budget, source.width(), source.height());
    if (!destination) {
        return std::unexpected(destination.error());
    }
    const auto scratch = methods::scratch_bytes(method, source.width(), 1);
    if (!scratch) {
        return std::unexpected(scratch.error());
    }
    const auto concurrency = exec::Concurrency::resolve(std::nullopt, exec::detected_concurrency(),
                                                        budget.available(), *scratch);
    if (!concurrency) {
        return std::unexpected(concurrency.error());
    }
    const exec::Scheduler scheduler{*concurrency, cancellation};
    const auto applied = methods::binarize(source.view().as_const(), destination->view(), method,
                                           {.scheduler = scheduler, .budget = budget});
    if (!applied) {
        return std::unexpected(applied.error());
    }
    auto published = publish_run({
        .output_directory = request.output_directory(),
        .artwork = BinaryArtwork{.samples = destination->view().as_const(), .method = method},
        .budget = budget,
        .cancellation = cancellation,
        .context = context,
        .source = loaded->source,
        .source_name = io::file_name(request.input()),
        .source_decoding = loaded->description,

    });
    if (!published) {
        return std::unexpected(std::move(published.error()));
    }
    return app::PublishedBinary{
        .output = std::move(published->output),
        .run = std::move(published->run),
        .record = std::move(published->record),
        .source_decoding = loaded->description,
    };
}
} // namespace
app::ProcessResult Processor::process(const app::ProcessRequest& request,
                                      const core::Cancellation& cancellation) {
    if (!request.ready()) {
        return app::process_failure({
            .code = core::ErrorCode::argument,
            .message = "Processing request no longer owns valid admitted paths",
        });
    }

    if (cancellation.requested(core::Checkpoint::admission)) {
        return app::process_failure(core::cancelled().error());
    }
    if (const auto* const method = std::get_if<methods::Binarization>(&request.operation())) {
        auto result = binary(request, *method, cancellation, context_);
        if (!result) {
            return app::process_failure(std::move(result.error()));
        }
        return std::move(*result);
    }
    methods::IlluminationReport report;
    methods::DenoisingReport denoising;
    auto result =
        continuous(request, std::get<image::Continuous>(request.operation()),
                   {.cancellation = cancellation, .context = context_}, report, denoising);
    if (!result) {
        if (report.requested && !report.complete) {
            report.status = methods::SurfaceStatus::failed;
            if (report.reason == methods::SurfaceReason::none ||
                report.reason == methods::SurfaceReason::no_effect) {
                report.reason = methods::SurfaceReason::processing_failure;
            }
        }
        if (denoising.requested && !denoising.complete) {
            denoising.status = methods::DenoiseStatus::failed;
            denoising.reason = methods::DenoiseReason::processing_failure;
        }
        return app::process_failure(std::move(result.error()), report, denoising);
    }
    return std::move(*result);
}
} // namespace docenhance::host
