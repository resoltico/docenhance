// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/pipeline/render.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/codec/png_output.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "png_fixture.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace docenhance::tests {
namespace {
pipeline::Input render_input(core::Budget& budget, std::span<const std::uint8_t> bytes) {
    pipeline::Input input;
    input.encoded().source = budget.allocate(bytes.size()).value();
    std::ranges::copy(std::as_bytes(bytes), input.encoded().source.bytes().begin());
    input.encoded().source_identity = fs::identify(input.encoded().source.bytes()).value();
    return input;
}
std::vector<std::uint8_t> render_fixture() {
    return make_gray_png(
        {.width = 2, .height = 2, .depth = 8, .interlaced = false, .samples = {0, 255, 128, 64}});
}
} // namespace
TEST_CASE("Eager binary rendering compares actual PNG1 and records only completed families",
          "[pipeline]") {
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(2, 2, 0, 0).value()};
    methods::Plan plan;
    plan.output = methods::BinaryOutput{.method = methods::FixedThreshold::create(0.5).value()};
    {
        auto input = render_input(budget, render_fixture());
        auto rendered = pipeline::render(std::move(input), plan, budget, scheduler);
        REQUIRE(rendered);
        // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        CHECK(!input.ready());
        CHECK(rendered->output.decoded_and_compared);
        CHECK(rendered->output.shape.depth.bits() == 1);
        CHECK(!rendered->output.profile_embedded);
        CHECK(!rendered->stages.illumination);
        CHECK(!rendered->stages.denoising);
        CHECK(!rendered->stages.restoration);
        CHECK(!rendered->stages.contrast);
        CHECK(!rendered->stages.sharpening);
        REQUIRE(rendered->stages.binarization.has_value());
        if (!rendered->stages.binarization.has_value()) {
            throw std::logic_error("Required test observation is absent");
        }
        const auto& facts = std::get<methods::FixedFacts>(rendered->stages.binarization.value());
        CHECK(facts.black_samples == 2);
        CHECK(facts.white_samples == 2);
        const std::array<std::uint8_t, 4> expected{0, 1, 1, 0};
        const auto pixels = image::PlaneView<const std::uint8_t>::create(
                                expected, {.width = 2, .height = 2, .stride = 2})
                                .value();
        REQUIRE(codec::compare_png(rendered->result.bytes(),
                                   {.shape = rendered->output.shape,
                                    .pixels = pixels,
                                    .profile = std::span<const std::byte>(),
                                    .resolution = std::nullopt},
                                   budget));
    }
    CHECK(budget.used() == 0);
}
TEST_CASE("Acquired empty input remains codec input while moved input precedes pending stop",
          "[pipeline][ownership]") {
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    auto empty = render_input(budget, {});
    REQUIRE(empty.ready());
    auto acquired = std::move(empty);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK(!empty.ready());
    const CheckpointStop stop{core::Checkpoint::processing, 0};
    const exec::Scheduler stopped{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                  stop.cancellation()};
    const methods::Plan plan;
    // NOLINTNEXTLINE(bugprone-use-after-move)
    const auto moved = pipeline::render(std::move(empty), plan, budget, stopped);
    REQUIRE(!moved);
    CHECK(moved.error().error.code == core::ErrorCode::argument);
    CHECK(CheckpointStop::visits() == 0);
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto malformed = pipeline::render(std::move(acquired), plan, budget, scheduler);
    REQUIRE(!malformed);
    CHECK(malformed.error().error.code == core::ErrorCode::input);
    CHECK(!malformed.error().failed_stage);
    CHECK(budget.used() == 0);
}
TEST_CASE("Short rendering budgets refuse with complete refunds and no output",
          "[pipeline][resource]") {
    const auto bytes = render_fixture();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const methods::Plan plan;
    for (const auto limit : {std::size_t{1024}, std::size_t{65536}}) {
        core::Budget budget{limit};
        {
            auto input = render_input(budget, bytes);
            const auto rendered = pipeline::render(std::move(input), plan, budget, scheduler);
            REQUIRE(!rendered);
            CHECK(rendered.error().error.code == core::ErrorCode::resource);
            CHECK(!rendered.error().completed.illumination);
            CHECK(!rendered.error().completed.binarization);
        }
        CHECK(budget.used() == 0);
    }
}
TEST_CASE("A real kernel refusal retains only actually completed earlier families",
          "[pipeline][failure]") {
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(2, 2, 0, 0).value()};
    methods::Plan plan;
    plan.photometric.illumination = methods::Morphology::create({.strength = 0}).value();
    plan.photometric.denoising = methods::Tvl1::create({.blend = 0}).value();
    plan.photometric.restoration = methods::Wiener::create({.psf = methods::Kernel{}}).value();
    {
        auto input = render_input(budget, render_fixture());
        input.encoded().psf = budget.allocate(1).value();
        input.encoded().psf.bytes().front() = std::byte{'x'};
        input.encoded().psf_identity = fs::identify(input.encoded().psf.bytes()).value();
        const auto rendered = pipeline::render(std::move(input), plan, budget, scheduler);
        REQUIRE(!rendered);
        const auto& failure = rendered.error();
        CHECK(failure.error.code == core::ErrorCode::input);
        CHECK(failure.failed_stage == methods::Family::restoration);
        REQUIRE(failure.completed.illumination.has_value());
        if (!failure.completed.illumination.has_value()) {
            throw std::logic_error("Required test observation is absent");
        }
        REQUIRE(failure.completed.denoising.has_value());
        if (!failure.completed.denoising.has_value()) {
            throw std::logic_error("Required test observation is absent");
        }
        CHECK(std::holds_alternative<methods::MorphologyFacts>(
            failure.completed.illumination.value()));
        CHECK(std::holds_alternative<methods::Tvl1Facts>(failure.completed.denoising.value()));
        CHECK(!failure.completed.restoration);
        CHECK(!failure.completed.contrast);
        CHECK(!failure.completed.sharpening);
        CHECK(!failure.completed.binarization);
        CHECK(failure.warning_context.interpretation.has_value());
        CHECK(failure.warning_context.restoration_entered);
        CHECK(!failure.warning_context.resolved_psf_centroid);
        CHECK(!failure.warning_context.observed_tvl1_stop);
    }
    CHECK(budget.used() == 0);
}
TEST_CASE("A bypassed kernel retains the same encoded buffer and only final owner charges",
          "[pipeline][resource]") {
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    methods::Plan plan;
    plan.photometric.restoration =
        methods::Wiener::create({.psf = methods::Kernel{}, .blend = 0}).value();
    plan.output = methods::BinaryOutput{.method = methods::FixedThreshold::create(0.5).value()};
    const auto psf = make_gray_png({
        .width = 3,
        .height = 3,
        .depth = 8,
        .interlaced = false,
        .samples = {0, 0, 0, 0, 255, 0, 0, 0, 0},
    });
    {
        auto input = render_input(budget, render_fixture());
        input.encoded().psf = budget.allocate(psf.size()).value();
        std::ranges::copy(std::as_bytes(std::span{psf}), input.encoded().psf.bytes().begin());
        const auto identity = fs::identify(input.encoded().psf.bytes()).value();
        input.encoded().psf_identity = identity;
        const auto* const address = input.encoded().psf.bytes().data();
        auto rendered = pipeline::render(std::move(input), plan, budget, scheduler);
        REQUIRE(rendered);
        REQUIRE(rendered->psf);
        if (!rendered->psf.has_value()) {
            throw std::logic_error("Required test observation is absent");
        }
        CHECK(rendered->psf.value().encoded.bytes().data() == address);
        CHECK(rendered->psf.value().identity == identity);
        CHECK(std::ranges::equal(rendered->psf.value().encoded.bytes(),
                                 std::as_bytes(std::span{psf})));
        REQUIRE(rendered->stages.restoration.has_value());
        if (!rendered->stages.restoration.has_value()) {
            throw std::logic_error("Required test observation is absent");
        }
        CHECK(rendered->stages.restoration.value().reason == methods::WienerReason::zero_blend);
        CHECK(rendered->stages.restoration.value().counts.eligible == 4);
        CHECK(!rendered->stages.restoration.value().transform);
        CHECK(budget.used() == 8192 + psf.size());
    }
    CHECK(budget.used() == 0);
}
} // namespace docenhance::tests
