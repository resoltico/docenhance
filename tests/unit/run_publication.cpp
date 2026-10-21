// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "run_publication.hpp"

#include "bundle_verify.hpp"
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/execution_limits.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/host/processor.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/pipeline/render.hpp"
#include "docenhance/record/record.hpp"
#include "png_fixture.hpp"
#include "temporary_directory.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace docenhance::tests {
namespace {
record::RunContext execution_context() {
    return {.identity = std::string(32, 'b'), .recorded = "2026-10-19T00:00:00Z"};
}
app::ProcessRequest publication_request(const std::filesystem::path& output) {
    app::Invocation invocation;
    invocation.command = app::Command::process;
    invocation.operands = {"source.png"};
    invocation.option(app::OptionId::out_dir) = utf8_spelling(output);
    invocation.option(app::OptionId::output_mode) = "bw";
    invocation.option(app::OptionId::binarize) = "fixed";
    auto request = app::prepare_process(invocation);
    REQUIRE(request);
    return std::move(*request);
}
pipeline::Rendering publication_rendering(core::Budget& budget, const exec::Scheduler& scheduler,
                                          const methods::Plan& plan,
                                          core::ContentIdentity& source) {
    const auto bytes = make_gray_png(
        {.width = 2, .height = 2, .depth = 8, .interlaced = false, .samples = {0, 255, 128, 64}});
    auto storage = budget.allocate(bytes.size());
    REQUIRE(storage);
    std::ranges::copy(std::as_bytes(std::span{bytes}), storage->bytes().begin());
    const auto identity = fs::identify(storage->bytes());
    REQUIRE(identity);
    source = *identity;
    pipeline::Input input;
    input.encoded().source = std::move(*storage);
    input.encoded().source_identity = source;
    auto rendered = pipeline::render(std::move(input), plan, budget, scheduler);
    REQUIRE(rendered);
    REQUIRE(rendered->output.decoded_and_compared);
    return std::move(*rendered);
}
app::ProcessFailure publication_failure(const pipeline::Rendering& rendering,
                                        const core::Budget& budget) {
    app::ProcessFailure failure{
        .error = {.code = core::ErrorCode::resource, .message = "Processing allocation failed"},
        .diagnostics =
            app::Diagnostics{
                .threads = 1,
                .memory_limit_mib = core::default_memory_limit / (std::size_t{1024} * 1024),
                .charged_peak_bytes = budget.peak(),
            },
    };
    static_cast<methods::StageReports&>(failure.completed) = rendering.stages;
    failure.warning_context.interpretation = rendering.interpretation;
    return failure;
}
} // namespace
TEST_CASE("Unchecked PNG comparison refuses before effects and preserves a publishable handoff",
          "[host][publication]") {
    const TemporaryDirectory temporary{"docenhance-run-publication"};
    core::Budget budget{core::default_memory_limit};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    const auto request = publication_request(temporary.path / "result");
    core::ContentIdentity source;
    auto rendered = publication_rendering(budget, scheduler, request.plan(), source);
    const record::RunContext context{
        .identity = std::string(32, 'a'),
        .recorded = "2026-10-19T00:00:00Z",
    };
    const auto stages = rendered.stages;
    const auto charged = budget.used();
    const auto* const encoded_address = rendered.result.bytes().data();
    bool publication_started = false;
    host::RunPublication run{
        .request = request,
        .rendering = rendered,
        .budget = budget,
        .scheduler = scheduler,
        .context = context,
        .source = source,
        .source_name = "source.png",
        .mask = std::nullopt,
        .publication_started = publication_started,
    };
    auto failure = publication_failure(rendered, budget);
    rendered.output.decoded_and_compared = false;
    const auto refused = host::publish_run(run, failure);
    REQUIRE(!refused);
    CHECK(refused.error().error.code == core::ErrorCode::invariant);
    CHECK(refused.error().publication == core::Publication::not_started);
    CHECK(static_cast<const methods::StageReports&>(refused.error().completed) == stages);
    CHECK(refused.error().warning_context.interpretation == rendered.interpretation);
    CHECK(!publication_started);
    CHECK(empty_directory(temporary.path));
    CHECK(run.context == context);
    CHECK(run.source == source);
    CHECK(run.source_name == "source.png");
    CHECK(rendered.stages == stages);
    CHECK(rendered.result.bytes().data() == encoded_address);
    CHECK(budget.used() == charged);
    failure = publication_failure(rendered, budget);
    rendered.output.decoded_and_compared = true;
    const auto published = host::publish_run(run, failure);
    REQUIRE(published);
    CHECK(publication_started);
    CHECK(published->record.context == context);
    CHECK(static_cast<const methods::StageReports&>(published->record.execution) == stages);
    CHECK(published->record.source.sha256 == source.sha256);
    CHECK(published->record.source.bytes == source.bytes);
    const auto verified = host::verify_bundle(request.output_directory(), {});
    REQUIRE(verified);
    CHECK(verified->declared == published->record);
    CHECK(verified->record == published->record_identity);
    REQUIRE(verified->declared.execution.binarization.has_value());
    if (!verified->declared.execution.binarization.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    const auto& facts =
        std::get<methods::FixedFacts>(verified->declared.execution.binarization.value());
    CHECK(facts.black_samples == 2);
    CHECK(facts.white_samples == 2);
    CHECK(!std::filesystem::exists(temporary.path / "result.staging-0"));
}
TEST_CASE(
    "Selected sixty four MiB ledger refusal reports actual failure diagnostics before decode") {
    const TemporaryDirectory temporary{"docenhance-selected-budget"};
    const auto input = temporary.path / "snapshot.bin";
    {
        const std::ofstream create{input, std::ios::binary};
        REQUIRE(create.good());
    }
    const auto control = app::ExecutionControl::create(1, std::uint64_t{64} * 1024 * 1024);
    REQUIRE(control);
    const auto requested_bytes = control.value().memory_limit() + 1;
    REQUIRE(requested_bytes < core::source_encoded_bytes_max);
    // An owned regular source exercises the snapshot ledger before any codec/parser work.
    // Extending it may be sparse; this is a budget-preflight fixture, not a claimed valid image.
    std::filesystem::resize_file(input, requested_bytes);
    app::Invocation invocation;
    invocation.command = app::Command::process;
    invocation.operands = {utf8_spelling(input)};
    const auto output = temporary.path / "result";
    invocation.option(app::OptionId::out_dir) = utf8_spelling(output);
    const auto request = app::prepare_process(invocation);
    REQUIRE(request);
    host::Processor processor{execution_context};
    const auto failed = processor.process(request.value(), control.value());
    REQUIRE(!failed);
    CHECK(failed.error().error.code == core::ErrorCode::resource);
    CHECK(failed.error().error.message.contains("64 MiB"));
    CHECK(failed.error().publication == core::Publication::not_started);
    REQUIRE(failed.error().diagnostics.has_value());
    if (!failed.error().diagnostics.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    const auto& diagnostics = failed.error().diagnostics.value();
    CHECK(diagnostics.threads == 1);
    CHECK(diagnostics.memory_limit_mib == 64);
    CHECK(diagnostics.charged_peak_bytes <= control.value().memory_limit());
    CHECK(!std::filesystem::exists(output));
    CHECK(std::filesystem::file_size(input) == requested_bytes);
}
} // namespace docenhance::tests
