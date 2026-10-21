// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/clahe.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/levels.hpp"
#include "docenhance/methods/morphology_working.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_working.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/surface_working.hpp"
#include "docenhance/methods/tvl1_working.hpp"
#include "docenhance/methods/unsharp.hpp"
#include "docenhance/methods/working.hpp"
#include "docenhance/pipeline/render.hpp"
#include "gamma_fixture.hpp"
#include "nlm_source_fixture.hpp"
#include "png_fixture.hpp"
#include "require.hpp"
#include "restoration_working_fixture.hpp"
#include "scalar_boundary_cases.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>
namespace docenhance::tests {
namespace {
enum class Consumer { surface, morphology, tvl1, levels, clahe, sharpening, restoration };
constexpr std::array consumers{
    Consumer::surface, Consumer::morphology, Consumer::tvl1,        Consumer::levels,
    Consumer::clahe,   Consumer::sharpening, Consumer::restoration,
};
constexpr std::array invalid_samples{
    -0.01,
    1.01,
    std::numeric_limits<double>::infinity(),
    -std::numeric_limits<double>::infinity(),
    std::numeric_limits<double>::quiet_NaN(),
};
methods::Plan selected_plan(Consumer consumer) {
    methods::Plan plan;
    switch (consumer) {
    case Consumer::surface:
        plan.photometric.illumination = methods::Surface::create().value();
        break;
    case Consumer::morphology:
        plan.photometric.illumination = methods::Morphology::create().value();
        break;
    case Consumer::tvl1:
        plan.photometric.denoising = methods::Tvl1::create().value();
        break;
    case Consumer::levels:
        plan.photometric.contrast = methods::Levels::create().value();
        break;
    case Consumer::clahe:
        plan.photometric.contrast =
            methods::Clahe::create({.grid_columns = 2, .grid_rows = 2}).value();
        break;
    case Consumer::sharpening:
        plan.photometric.sharpening = methods::Unsharp::create().value();
        break;
    case Consumer::restoration:
        plan.photometric.restoration = methods::Wiener::create().value();
        break;
    }
    return plan;
}
pipeline::Input scalar_input(core::Budget& budget, bool corrupt) {
    auto png = make_gray_png({
        .width = 32,
        .height = 32,
        .depth = 8,
        .interlaced = false,
        .samples = std::vector<std::uint8_t>(1024, 127),
    });
    if (corrupt) {
        png.at(29) ^= std::uint8_t{1};
    }
    pipeline::Input input;
    input.encoded().source = budget.allocate(png.size()).value();
    std::ranges::copy(std::as_bytes(std::span{png}), input.encoded().source.bytes().begin());
    input.encoded().source_identity = fs::identify(input.encoded().source.bytes()).value();
    return input;
}
void no_completed(const pipeline::Failure& failure) {
    CHECK(!failure.failed_stage);
    CHECK(!failure.completed.illumination);
    CHECK(!failure.completed.denoising);
    CHECK(!failure.completed.restoration);
    CHECK(!failure.completed.contrast);
    CHECK(!failure.completed.sharpening);
    CHECK(!failure.completed.binarization);
}
core::Result<void> apply_consumer(Consumer consumer, methods::Working working) {
    const auto discard = [](const auto&) {};
    switch (consumer) {
    case Consumer::surface:
        return methods::apply(methods::Surface::create().value(), working).transform(discard);
    case Consumer::morphology:
        return methods::apply(methods::Morphology::create().value(), working).transform(discard);
    case Consumer::tvl1: {
        std::optional<methods::Tvl1Stop> observed;
        return methods::apply(methods::Tvl1::create().value(), working, observed)
            .transform(discard);
    }
    case Consumer::levels:
        return methods::apply(methods::Levels::create().value(), working).transform(discard);
    case Consumer::clahe:
        return methods::apply(methods::Clahe::create({.grid_columns = 2, .grid_rows = 2}).value(),
                              working)
            .transform(discard);
    case Consumer::sharpening:
        return methods::apply(methods::Unsharp::create().value(), working).transform(discard);
    case Consumer::restoration: {
        const auto psf = restoration_gaussian();
        return methods::apply(methods::Wiener::create().value(), psf, working).transform(discard);
    }
    }
    return core::failure(core::ErrorCode::invariant, "Unknown test consumer");
}
} // namespace
TEST_CASE("Scalar boundary quantization rejects invalid samples before integer conversion") {
    const auto before = assertion_counter().load();
    quantization_domain_cases();
    REQUIRE(assertion_counter().load() > before);
}
TEST_CASE("Every actual pipeline consumer preserves genuine upstream PNG errors before work") {
    for (const auto consumer : consumers) {
        core::Budget budget{std::size_t{16} * 1024 * 1024};
        const auto plan = selected_plan(consumer);
        const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
        {
            const auto healthy =
                pipeline::render(scalar_input(budget, false), plan, budget, scheduler);
            REQUIRE(healthy);
            CHECK(healthy->output.decoded_and_compared);
        }
        CHECK(budget.used() == 0);
        {
            const auto malformed =
                pipeline::render(scalar_input(budget, true), plan, budget, scheduler);
            REQUIRE(!malformed);
            CHECK(malformed.error().error.code == core::ErrorCode::input);
            no_completed(malformed.error());
        }
        CHECK(budget.used() == 0);
        {
            const CheckpointStop stop{core::Checkpoint::decode, 0};
            const exec::Scheduler stopped{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                          stop.cancellation()};
            const auto cancelled =
                pipeline::render(scalar_input(budget, false), plan, budget, stopped);
            REQUIRE(!cancelled);
            CHECK(cancelled.error().error.code == core::ErrorCode::cancelled);
            REQUIRE(CheckpointStop::stopped());
            no_completed(cancelled.error());
        }
        CHECK(budget.used() == 0);
    }
}
TEST_CASE("Actual producer admission covers every RGB component and immutable method blend") {
    const image::Rgb healthy{0.1, 0.3, 0.7};
    REQUIRE(image::valid_unit_samples(healthy));
    REQUIRE(methods::Nlm::create({.blend = 0.5}));
    for (const auto invalid : invalid_samples) {
        for (const auto channel : {0U, 1U, 2U}) {
            auto rgb = healthy;
            rgb.at(channel) = invalid;
            const auto before = gamma_bits(rgb);
            CHECK(!image::valid_unit_samples(rgb));
            CHECK(gamma_bits(rgb) == before);
        }
        const auto blend = methods::Nlm::create({.blend = invalid});
        REQUIRE(!blend);
        CHECK(blend.error().code == core::ErrorCode::argument);
    }
}
TEST_CASE("Working consumers reject cold invalid roles before cancellation and preserve input") {
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    auto source = image::Plane<double>::allocate(budget, 32, 32).value();
    auto wrong_mask = image::Plane<std::uint8_t>::allocate(budget, 31, 32).value();
    std::ranges::fill(source.view().storage(), 0.5);
    std::ranges::fill(wrong_mask.view().storage(), 0);
    std::stop_source stop;
    static_cast<void>(request_stop(stop));
    const exec::Scheduler healthy{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const exec::Scheduler stopped{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                  core::Cancellation{stop.get_token()}};
    for (const auto consumer : consumers) {
        REQUIRE(apply_consumer(consumer, {source.view(), {}, budget, healthy}));
        std::ranges::fill(source.view().storage(), 0.5);
        const auto original = gamma_bits(source.view().storage());
        const auto held = budget.used();
        const auto mismatched = apply_consumer(
            consumer, {source.view(), wrong_mask.view().as_const(), budget, stopped});
        REQUIRE(!mismatched);
        CHECK(mismatched.error().code == core::ErrorCode::argument);
        const auto empty = apply_consumer(consumer, {{}, {}, budget, stopped});
        REQUIRE(!empty);
        CHECK(empty.error().code == core::ErrorCode::argument);
        const auto cancelled = apply_consumer(consumer, {source.view(), {}, budget, stopped});
        REQUIRE(!cancelled);
        CHECK(cancelled.error().code == core::ErrorCode::cancelled);
        CHECK(gamma_bits(source.view().storage()) == original);
        CHECK(budget.used() == held);
    }
}
TEST_CASE("Protected context is admitted by the real source before any illumination consumer") {
    for (const auto x : {0U, 1U}) {
        for (const auto channel : {0U, 1U, 2U}) {
            core::Budget budget{nlm_source_budget};
            {
                NlmSourceFixture fixture{budget,
                                         image::SampleModel::rgba,
                                         {},
                                         NlmAssociatedFault{.x = x, .channel = channel}};
                CHECK((fixture.protection.view().row(0).subspan(x, 1).front() != 0) == (x == 0));
                const auto before = gamma_bits(fixture.luminance.view().storage());
                const auto held = budget.used();
                const auto read = extract_nlm_source(fixture);
                REQUIRE(!read);
                CHECK(read.error().code == core::ErrorCode::input);
                CHECK(read.error().message == "Associated TIFF color exceeds alpha");
                CHECK(gamma_bits(fixture.luminance.view().storage()) == before);
                CHECK(budget.used() == held);
            }
            CHECK(budget.used() == 0);
        }
    }
}
} // namespace docenhance::tests
