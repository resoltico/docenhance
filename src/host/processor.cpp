// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/host/processor.hpp"

#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fs/paths.hpp"
#include "docenhance/fs/snapshot.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/pipeline/render.hpp"
#include "docenhance/record/record.hpp"
#include "run_publication.hpp"

#include <cstdint>
#include <expected>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>
namespace docenhance::host {
namespace {
// Owned error/context and budget survive unwind; no return metadata is allocated after effect.
struct Processing {
    app::ProcessFailure failure{
        .error = {.code = core::ErrorCode::resource, .message = "Processing allocation failed"},
    };
    std::optional<core::Budget> budget;
    bool publication_started = false;
    [[nodiscard]] app::ProcessResult refused() {
        if (budget && failure.diagnostics) {
            failure.diagnostics->charged_peak_bytes = budget->peak();
        }
        return std::unexpected(std::move(failure));
    }
    [[nodiscard]] app::ProcessResult escaped(core::Error unknown) {
        if (publication_started) {
            // A publisher that escaped supplied no confirmed state; preserve the ambiguity.
            failure.error = std::move(unknown);
            failure.publication = core::Publication::unknown;
        }
        return refused();
    }
};
core::Result<pipeline::Input> acquire(const app::ProcessRequest& request, core::Budget& budget,
                                      const core::Cancellation& cancellation) {
    auto source = fs::read_source_snapshot(request.input(), budget, cancellation,
                                           core::source_encoded_bytes_max);
    if (!source) {
        return std::unexpected(std::move(source.error()));
    }
    pipeline::Input input;
    input.encoded().source = std::move(source->encoded);
    input.encoded().source_identity = std::move(source->identity);
    if (request.mask_path()) {
        auto mask = fs::read_source_snapshot(*request.mask_path(), budget, cancellation,
                                             core::source_encoded_bytes_max);
        if (!mask) {
            return std::unexpected(std::move(mask.error()));
        }
        input.encoded().mask = std::move(mask->encoded);
        input.encoded().mask_identity = std::move(mask->identity);
    }
    if (request.psf_path()) {
        auto psf = fs::read_source_snapshot(*request.psf_path(), budget, cancellation,
                                            core::psf_encoded_bytes_max);
        if (!psf) {
            return std::unexpected(std::move(psf.error()));
        }
        input.encoded().psf = std::move(psf->encoded);
        input.encoded().psf_identity = std::move(psf->identity);
    }
    return input;
}
app::ProcessResult render_and_publish(const app::ProcessRequest& request,
                                      record::RunContext context, const exec::Scheduler& scheduler,
                                      Processing& processing) {
    if (!processing.budget) {
        processing.failure.error = {
            .code = core::ErrorCode::invariant,
            .message = "Processing budget was not initialized",
        };
        return processing.refused();
    }
    auto& budget = *processing.budget;
    auto input = acquire(request, budget, scheduler.cancellation());
    if (!input) {
        processing.failure.error = std::move(input.error());
        return processing.refused();
    }
    // These small identities survive the consumed snapshots; the source is never reopened.
    auto source = input->encoded().source_identity;
    auto mask = input->encoded().mask_identity;
    auto source_name = fs::file_name(request.input());
    auto rendered = pipeline::render(std::move(*input), request.plan(), budget, scheduler);
    if (!rendered) {
        processing.failure.error = std::move(rendered.error().error);
        processing.failure.failed_stage = rendered.error().failed_stage;
        processing.failure.completed = rendered.error().completed;
        processing.failure.warning_context = rendered.error().warning_context;
        return processing.refused();
    }
    static_cast<methods::StageReports&>(processing.failure.completed) = rendered->stages;
    processing.failure.warning_context.interpretation = rendered->interpretation;
    RunPublication run{
        .request = request,
        .rendering = *rendered,
        .budget = budget,
        .scheduler = scheduler,
        .context = std::move(context),
        .source = std::move(source),
        .source_name = std::move(source_name),
        .mask = std::move(mask),
        .publication_started = processing.publication_started,
    };
    return publish_run(run, processing.failure);
}
} // namespace
app::ProcessResult Processor::process(const app::ProcessRequest& request,
                                      const app::ExecutionControl& control) {
    const auto cancellation = control.cancellation();
    static_assert(std::is_nothrow_move_constructible_v<app::ProcessFailure>);
    static_assert(std::is_nothrow_move_assignable_v<app::ProcessFailure>);
    static_assert(std::is_nothrow_copy_assignable_v<methods::StageReports>);
    static_assert(std::is_nothrow_copy_assignable_v<image::InterpretationReport>);
    Processing processing;
    core::Error unexpected{
        .code = core::ErrorCode::invariant,
        .message = "Unexpected processing failure",
    };
    core::Error unknown{
        .code = core::ErrorCode::publication_unknown,
        .message = "Processing did not report publication; inspect output before retrying",
    };
    try {
        if (!request.ready()) {
            return app::process_failure(
                {
                    .code = core::ErrorCode::argument,
                    .message = "Processing request no longer owns valid admitted paths",
                },
                core::Publication::not_started);
        }
        if (cancellation.requested(core::Checkpoint::admission)) {
            return app::process_failure(core::cancelled().error(), core::Publication::not_started);
        }
        auto context = context_source_();
        if (!record::validate(context)) {
            return app::process_failure(
                {
                    .code = core::ErrorCode::invariant,
                    .message = "Invalid execution run context",
                },
                core::Publication::not_started);
        }
        processing.budget.emplace(control.memory_limit());
        const auto& budget = *processing.budget;
        auto concurrency = exec::Concurrency::resolve(
            control.workers(), exec::detected_concurrency(), budget.available(), 0);
        if (!concurrency) {
            processing.failure.error = std::move(concurrency.error());
            return processing.refused();
        }
        const exec::Scheduler scheduler{*concurrency, cancellation};
        constexpr auto mib = std::uint64_t{1024} * 1024;
        processing.failure.diagnostics = app::Diagnostics{
            .threads = scheduler.workers(),
            .memory_limit_mib = budget.limit() / mib,
            .charged_peak_bytes = budget.peak(),
        };
        return render_and_publish(request, std::move(context), scheduler, processing);
    } catch (const std::bad_alloc&) {
        // The resource diagnostic was allocated before any processing or publication effect.
        return processing.escaped(std::move(unknown));
    } catch (...) {
        processing.failure.error = std::move(unexpected);
        return processing.escaped(std::move(unknown));
    }
}
} // namespace docenhance::host
