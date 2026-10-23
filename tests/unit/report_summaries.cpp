// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "delivery_fixture.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "docenhance/report/render.hpp"
#include "record_fixture.hpp"
#include "restoration_fixture.hpp"
#include "stage_report_fixture.hpp"

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace docenhance::tests {
namespace {
app::ProcessFailure summary_failure(const methods::StageReports& facts = {}) {
    app::ProcessFailure failure{
        .error = {.code = core::ErrorCode::resource, .message = "Observed allocation refusal"},
    };
    static_cast<methods::StageReports&>(failure.completed) = facts;
    return failure;
}
app::Outcome summary_outcome(app::ProcessFailure failure) {
    return {
        .command = app::Command::process,
        .build = core::build_facts(),
        .payload = std::move(failure),
    };
}
std::string failure_summary(app::ProcessFailure failure) {
    const auto text = report::render(summary_outcome(std::move(failure)), report::Format::text);
    REQUIRE(text);
    CHECK(text->out.empty());
    return text->err;
}
methods::Tvl1Solution small_update_solution() {
    return {
        .iterations = 10,
        .stop = methods::Tvl1Stop::iteration_limit,
        .passing_checkpoints = 0,
        .primal_update = 1e-12,
        .dual_update = 2e-12,
        .objective_start = 4,
        .objective_end = 3,
    };
}
methods::Tvl1Facts small_update_facts() {
    return {
        .disposition = methods::Disposition::applied,
        .reason = methods::Tvl1Reason::none,
        .counts = {.eligible = 8, .evaluated = 8, .changed = 3},
        .context_samples = 8,
        .corrected_samples = 4,
        .solver = small_update_solution(),
    };
}
} // namespace

TEST_CASE("Human summaries distinguish absent stages from unreported nested facts", "[report]") {
    const auto absent = failure_summary(summary_failure());
    CHECK(absent.contains("Completed stage facts: none reported\n"));
    CHECK(!absent.contains("denoising:\n"));
    const methods::StageReports stages{
        .denoising =
            methods::Tvl1Facts{
                .reason = methods::Tvl1Reason::zero_blend,
                .counts = {.eligible = 8},
            },
    };
    REQUIRE(methods::validate(stages));
    const auto text = failure_summary(summary_failure(stages));
    CHECK(text.contains("Completed stage facts:\ndenoising:\n"));
    CHECK(text.contains("  disposition: \"no_change\"\n"));
    CHECK(text.contains("  reason: \"zero_blend\"\n"));
    CHECK(text.contains("  solver: not reported\n"));
    CHECK(text.contains("  counts:\n"));
    CHECK(text.contains("    changed: 0\n"));
    CHECK(text.contains("    eligible: 8\n"));
    CHECK(!text.contains("illumination:\n"));
    CHECK(!text.contains("binarization:\n"));
}

TEST_CASE("Human summaries retain all six stages in execution order", "[report]") {
    auto stages = selected_stage_reports();
    stages.illumination = methods::SurfaceFacts{
        .reason = methods::SurfaceReason::zero_strength,
        .counts = {.eligible = 512},
    };
    stages.restoration = methods::WienerFacts{
        .reason = methods::WienerReason::zero_blend,
        .counts = {.eligible = 512},
        .psf = analytic_psf_facts(),
    };
    stages.sharpening = methods::UnsharpFacts{
        .reason = methods::UnsharpReason::zero_amount,
        .counts = {.eligible = 512},
    };
    methods::OtsuFacts binary;
    binary.black_samples = 200;
    binary.white_samples = 312;
    binary.threshold_bin = 123;
    stages.binarization = binary;
    REQUIRE(methods::validate(stages));
    const auto before = stages;
    const auto text = failure_summary(summary_failure(stages));
    const std::array<std::string_view, 6> headings{
        "\nillumination:\n", "\ndenoising:\n",  "\nrestoration:\n",
        "\ncontrast:\n",     "\nsharpening:\n", "\nbinarization:\n",
    };
    auto previous = text.find(headings.front());
    REQUIRE(previous != std::string::npos);
    for (const auto heading : std::span{headings}.subspan(1)) {
        const auto position = text.find(heading);
        REQUIRE(position != std::string::npos);
        CHECK(position > previous);
        previous = position;
    }
    CHECK(text.contains("  weight_table_entries: 113337\n"));
    CHECK(text.contains("    changed: 100\n"));
    CHECK(text.contains("    changed: 50\n"));
    CHECK(text.contains("  psf:\n"));
    CHECK(text.contains("    centroid_x:"));
    CHECK(text.contains("    centroid_y:"));
    CHECK(text.contains("  black_samples: 200\n"));
    CHECK(text.contains("  white_samples: 312\n"));
    CHECK(text.contains("  single_bin_fallback: false\n"));
    CHECK(text.contains("  threshold_bin: 123\n"));
    CHECK(stages == before);
}

TEST_CASE("Human summaries keep tiny solver updates and sharpening excursions", "[report]") {
    const methods::StageReports stages{
        .denoising = small_update_facts(),
        .sharpening =
            methods::UnsharpFacts{
                .disposition = methods::Disposition::applied,
                .reason = methods::UnsharpReason::none,
                .counts = {.eligible = 8, .evaluated = 8, .changed = 3},
                .context_samples = 8,
                .corrected_samples = 5,
                .pre_clamp = methods::ExcursionRange{.low = -0.25, .high = 1.5},
                .clipped_low_samples = 1,
                .clipped_high_samples = 1,
            },
    };
    REQUIRE(methods::validate(stages));
    const auto text = failure_summary(summary_failure(stages));
    CHECK(text.contains("    primal_update: 1e-12\n"));
    CHECK(text.contains("    dual_update: 2e-12\n"));
    CHECK(text.contains("    iterations: 10\n"));
    CHECK(text.contains("    passing_checkpoints: 0\n"));
    CHECK(text.contains("    objective_start: 4.0\n"));
    CHECK(text.contains("    objective_end: 3.0\n"));
    CHECK(text.contains("    stop: \"iteration_limit\"\n"));
    CHECK(text.contains("  pre_clamp:\n"));
    CHECK(text.contains("    high: 1.5\n"));
    CHECK(text.contains("    low: -0.25\n"));
    CHECK(text.contains("  clipped_low_samples: 1\n"));
    CHECK(text.contains("  clipped_high_samples: 1\n"));
    CHECK(text.contains("W_TV_ITERATION_LIMIT"));
    CHECK(text.contains("not a convergence certificate"));
}

TEST_CASE("Human skipped summaries retain the observed protected population", "[report]") {
    const methods::StageReports stages{
        .illumination =
            methods::SurfaceFacts{
                .disposition = methods::Disposition::skipped,
                .reason = methods::SurfaceReason::insufficient_samples,
                .counts = {.eligible = 1, .protected_samples = 511},
            },
    };
    REQUIRE(methods::validate(stages));
    const auto text = failure_summary(summary_failure(stages));
    CHECK(text.contains("  disposition: \"skipped\"\n"));
    CHECK(text.contains("  reason: \"insufficient_samples\"\n"));
    CHECK(text.contains("    protected: 511\n"));
    CHECK(text.contains("  resolved_cell: not reported\n"));
    CHECK(text.contains("  auto_predicates:\n"));
    CHECK(text.contains("    coverage: not reported\n"));
    CHECK(!text.contains("denoising:\n"));
}

TEST_CASE("Partial TV failure keeps its cap warning without inventing completed facts",
          "[report]") {
    const methods::StageReports stages{
        .illumination =
            methods::SurfaceFacts{
                .reason = methods::SurfaceReason::zero_strength,
                .counts = {.eligible = 8},
            },
    };
    REQUIRE(methods::validate(stages));
    auto failure = summary_failure(stages);
    failure.failed_stage = methods::Family::denoising;
    failure.warning_context.observed_tvl1_stop = methods::Tvl1Stop::iteration_limit;
    failure.diagnostics = app::Diagnostics{
        .threads = 2,
        .memory_limit_mib = 512,
        .charged_peak_bytes = 4096,
    };
    const auto text = failure_summary(std::move(failure));
    CHECK(text.starts_with("E_RESOURCE: Observed allocation refusal\n"));
    CHECK(text.contains("Publication: not_started\n"));
    CHECK(text.contains("illumination:\n"));
    CHECK(text.contains("Execution: threads=2; memory limit=512 MiB; charged peak=4096 bytes\n"));
    CHECK(text.contains("W_TV_ITERATION_LIMIT"));
    CHECK(text.contains("not a convergence certificate"));
    CHECK(!text.contains("denoising:\n"));
    CHECK(!text.contains("primal_update:"));
}

TEST_CASE("Published binary summaries preserve classification counts and publication identity",
          "[report]") {
    app::Published published{
        .output = "result/result.png",
        .record_identity = core::ContentIdentity(),
        .record = record_fixture::binarized_record(),
        .diagnostics = {.threads = 1, .memory_limit_mib = 512, .charged_peak_bytes = 1024},
    };
    identify_delivery_record(published);
    const auto digest = published.record_identity.sha256;
    const auto text = report::render(
        {.command = app::Command::process, .build = core::build_facts(), .payload = published},
        report::Format::text);
    REQUIRE(text);
    CHECK(text->err.empty());
    CHECK(text->out.starts_with("Wrote verified PNG: result/result.png\n1 bits; 1 channels\n"));
    CHECK(text->out.contains("record SHA-256 " + digest + "\n"));
    CHECK(text->out.contains("Completed stage facts:\nbinarization:\n"));
    CHECK(text->out.contains("  black_samples: 2500\n"));
    CHECK(text->out.contains("  white_samples: 2500\n"));
    CHECK(text->out.contains("  method: \"B02\"\n"));
    CHECK(!text->out.contains("changed:"));
}

TEST_CASE("Human summaries refuse invalid counts cohorts enums and nonfinite facts", "[report]") {
    auto stages = selected_stage_reports();
    SECTION("Changed count exceeds the evaluated population") {
        REQUIRE(stages.denoising.has_value());
        if (!stages.denoising.has_value()) {
            throw std::logic_error("Required denoising fixture is absent");
        }
        std::get<methods::NlmFacts>(stages.denoising.value()).counts.changed = 513;
    }
    SECTION("Individually valid stages disagree on their eligible cohort") {
        REQUIRE(stages.contrast.has_value());
        if (!stages.contrast.has_value()) {
            throw std::logic_error("Required contrast fixture is absent");
        }
        auto& contrast = std::get<methods::GammaFacts>(stages.contrast.value());
        contrast.counts.eligible = 511;
        contrast.counts.evaluated = 511;
        REQUIRE(methods::validate(contrast));
    }
    SECTION("Unrecognized typed stop has no textual fallback") {
        auto facts = small_update_facts();
        auto solution = small_update_solution();
        solution.stop =
            std::bit_cast<methods::Tvl1Stop>(std::underlying_type_t<methods::Tvl1Stop>{99});
        facts.solver = solution;
        stages = {.denoising = facts};
    }
    SECTION("Nonfinite solver observation is not serialized as null") {
        auto facts = small_update_facts();
        auto solution = small_update_solution();
        solution.primal_update = std::numeric_limits<double>::quiet_NaN();
        facts.solver = solution;
        stages = {.denoising = facts};
    }
    REQUIRE(!methods::validate(stages));
    const auto result =
        report::render(summary_outcome(summary_failure(stages)), report::Format::text);
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::invariant);
}
} // namespace docenhance::tests
