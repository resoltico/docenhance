// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/warnings.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/tvl1.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "docenhance/methods/tvl1_working.hpp"
#include "docenhance/methods/working.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/report/render.hpp"
#include "docenhance/report/warnings.hpp"
#include "require.hpp"
#include "tvl1_detail.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace docenhance::tests {
namespace {
app::ProcessRequest warning_request(std::uint32_t cap) {
    app::Invocation invocation;
    invocation.command = app::Command::process;
    invocation.operands = {"in-memory-source.png"};
    invocation.option(app::OptionId::out_dir) = "unused-output";
    invocation.option(app::OptionId::output_mode) = "preserve";
    invocation.option(app::OptionId::denoise) = "tvl1";
    invocation.option(app::OptionId::tv_iterations) = std::to_string(cap);
    auto admitted = app::prepare_process(invocation);
    REQUIRE(admitted);
    return std::move(*admitted);
}
void failure_outputs(const app::ProcessFailure& failure, bool cap_warning) {
    const app::Outcome outcome{
        .command = app::Command::process,
        .build = core::build_facts(),
        .payload = failure,
    };
    const auto text = report::render(outcome, report::Format::text);
    REQUIRE(text);
    const auto json = report::render(outcome, report::Format::json);
    REQUIRE(json);
    const auto fields = record::Json::parse(json->out);
    const auto& warnings = fields.at("warnings");
    require(text->out.empty() && json->err.empty(),
            "ordinary failure delivery uses its real stream");
    require(text->err.contains("W_TV_ITERATION_LIMIT") == cap_warning,
            "human failure output preserves only an actually established cap");
    require(warnings == (cap_warning
                             ? record::Json::array({"W_PROFILE_ASSUMED", "W_TV_ITERATION_LIMIT"})
                             : record::Json::array({"W_PROFILE_ASSUMED"})),
            "JSON failure output agrees with human cap membership and real interpretation");
    require(fields.at("completed").empty() && fields.at("failed_stage") == "denoising" &&
                fields.at("diagnostics").is_null(),
            "partial solver failure fabricates no completed work or executor diagnostics");
    if (cap_warning) {
        const auto explanation = report::warning_explanation(core::Warning::tv_iteration_limit);
        require(explanation && text->err.contains(*explanation) &&
                    !text->err.contains("usable iterate was retained"),
                "actual failed text uses the sole factual explanation without retaining a model");
    }
    std::cout << "TV_FAILURE_TEXT_BEGIN\n"
              << text->err << "TV_FAILURE_TEXT_END\n"
              << "TV_FAILURE_JSON_BEGIN\n"
              << json->out << "TV_FAILURE_JSON_END\n";
}
std::array<image::Plane<double>, 5> partial_solver_planes(core::Budget& budget) {
    std::array<image::Plane<double>, 5> planes;
    for (std::size_t index = 0; index < planes.size(); ++index) {
        auto allocated = image::Plane<double>::allocate(budget, 1, 1);
        REQUIRE(allocated);
        planes.at(index) = std::move(*allocated);
        planes.at(index).view().row(0).front() = index < 3 ? image::srgb_encode(0.4) : 0;
    }
    return planes;
}
void partial_solver_case(const methods::Tvl1& method, std::optional<methods::Tvl1Stop> expected) {
    core::Budget budget{std::size_t{2} * 1024 * 1024};
    const auto resident = budget.used();
    {
        auto planes = partial_solver_planes(budget);
        const methods::Tvl1Field field{
            .input = planes.at(0).view().as_const(),
            .primal = planes.at(1).view(),
            .extrapolated = planes.at(2).view(),
            .dual_x = planes.at(3).view(),
            .dual_y = planes.at(4).view(),
        };
        const std::size_t allowed = expected ? 1 : 0;
        const CheckpointStop stop{core::Checkpoint::measurement, allowed};
        const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
        REQUIRE(concurrency);
        const exec::Scheduler scheduler{*concurrency, stop.cancellation()};
        methods::Tvl1SolveState progress;
        const auto solved = methods::solve_tvl1(field, method, scheduler, progress);
        REQUIRE(!solved);
        CHECK(solved.error().code == core::ErrorCode::cancelled);
        require(
            CheckpointStop::visits() == allowed + 1 && progress.stop == expected &&
                !progress.objective_end &&
                progress.iterations == (expected ? method.parameters().iterations : 0) &&
                progress.objective_start.has_value() == expected.has_value(),
            "actual private solver retains only established iterations stop and start objective");
        require(budget.used() > resident, "the actual five solver fields remain owned in scope");
    }
    require(budget.used() == resident, "all private solver fields refund after their owners end");
}
void failure_case(std::uint32_t cap, std::size_t allowed,
                  std::optional<methods::Tvl1Stop> expected_stop) {
    const auto request = warning_request(cap);
    const auto* method = std::get_if<methods::Tvl1>(&request.plan().photometric.denoising);
    REQUIRE(method != nullptr);
    partial_solver_case(*method, expected_stop);
    core::Budget budget{std::size_t{2} * 1024 * 1024};
    auto source = image::Plane<double>::allocate(budget, 1, 1);
    REQUIRE(source);
    source->view().row(0).front() = 0.4;
    const auto resident = budget.used();
    const CheckpointStop stop{core::Checkpoint::measurement, allowed};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency, stop.cancellation()};
    std::optional<methods::Tvl1Stop> observed;
    const auto facts = methods::apply(*method, {source->view(), {}, budget, scheduler}, observed);
    REQUIRE(!facts);
    require(facts.error().code == core::ErrorCode::cancelled && observed == expected_stop,
            "actual eager failure retains the observed stop without returning completed Facts");
    require(CheckpointStop::visits() == allowed + 1 && budget.used() == resident &&
                source->view().row(0).front() == 0.4,
            "actual objective boundary is reached every field refunds and unstarted mapping "
            "preserves Y");
    app::ProcessFailure failure{
        .error = facts.error(),
        .failed_stage = methods::Family::denoising,
    };
    failure.warning_context.interpretation = image::InterpretationReport{
        .decoded = {.width = 1, .height = 1},
        .output = {.width = 1, .height = 1, .depth = image::PngDepth()},
        .orientation = image::Orientation(),
        .rotation = image::QuarterTurn(),
        .resolution = std::nullopt,
        .assumed_transfer = true,
    };
    failure.warning_context.observed_tvl1_stop = observed;
    std::cout << "TV_FAILURE_OBSERVATIONS cap=" << cap << " allowed=" << allowed
              << " visits=" << CheckpointStop::visits() << " resident=" << resident
              << " refunded=" << budget.used() << '\n';
    failure_outputs(failure, expected_stop == methods::Tvl1Stop::iteration_limit);
}
} // namespace
TEST_CASE("TV-L1 failure keeps an observed cap in both ordinary outputs") {
    const auto assertions_before = assertion_counter().load();
    failure_case(10, 3, methods::Tvl1Stop::iteration_limit);
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("TV-L1 early objective cancellation has no cap warning") {
    const auto assertions_before = assertion_counter().load();
    failure_case(10, 2, std::nullopt);
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("TV-L1 tolerance at the cap stays quiet after objective cancellation") {
    const auto assertions_before = assertion_counter().load();
    failure_case(30, 3, methods::Tvl1Stop::tolerance_met);
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("TV-L1 healthy cap counterpart returns actual eager Facts and refunds all fields") {
    const auto assertions_before = assertion_counter().load();
    const auto request = warning_request(10);
    const auto* method = std::get_if<methods::Tvl1>(&request.plan().photometric.denoising);
    REQUIRE(method != nullptr);
    core::Budget budget{std::size_t{2} * 1024 * 1024};
    auto source = image::Plane<double>::allocate(budget, 1, 1);
    REQUIRE(source);
    source->view().row(0).front() = 0.4;
    const auto resident = budget.used();
    const CheckpointStop stop{core::Checkpoint::measurement,
                              std::numeric_limits<std::size_t>::max()};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency, stop.cancellation()};
    std::optional<methods::Tvl1Stop> observed;
    const auto facts = methods::apply(*method, {source->view(), {}, budget, scheduler}, observed);
    REQUIRE(facts);
    REQUIRE(facts->solver.has_value());
    if (!facts->solver.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    require(methods::validate(*facts, method->parameters()) &&
                facts->solver.value().iterations == 10 &&
                facts->solver.value().stop == methods::Tvl1Stop::iteration_limit &&
                observed == facts->solver.value().stop,
            "same request returns actual admitted completed solver Facts and retained stop");
    require(CheckpointStop::visits() == 4 && facts->solver.value().objective_start == 0 &&
                facts->solver.value().objective_end == 0 && facts->counts.evaluated == 1 &&
                facts->corrected_samples == 0 && facts->counts.changed == 0 &&
                budget.used() == resident && source->view().row(0).front() == 0.4,
            "healthy cap completes both objectives and mapping without retaining hidden workspace");
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
