// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/codec/png_output.hpp"
#include "docenhance/codec/protection_png.hpp"
#include "docenhance/color/profile.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/pipeline/render.hpp"
#include "png_fixture.hpp"
#include "png_transaction.hpp"
#include "temporary_directory.hpp"

#include <algorithm>
#include <atomic>
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
constexpr std::size_t byte_budget = std::size_t{32} * 1024 * 1024;
struct ContinuousFixture {
    image::Plane<std::uint8_t> pixels;
    image::Plane<std::uint8_t> profile;
    [[nodiscard]] codec::PngImage image() const {
        return {
            .shape = {.width = 8, .height = 8, .depth = image::PngDepth()},
            .pixels = pixels.view(),
            .profile = std::as_bytes(profile.view().row(0)),
            .resolution = std::nullopt,
        };
    }
};
ContinuousFixture continuous_fixture(core::Budget& budget) {
    auto pixels = image::Plane<std::uint8_t>::allocate(budget, 8, 8).value();
    std::ranges::fill(pixels.view().storage(), std::uint8_t{127});
    const auto canonical = color::make_output_profile(budget, true).value();
    auto profile = image::Plane<std::uint8_t>::allocate(
                       budget, static_cast<std::uint32_t>(canonical.size()), 1)
                       .value();
    // The typed test copy is charged too; the factory owner is released before codec work.
    std::ranges::transform(canonical.bytes(), profile.view().row(0).begin(),
                           [](std::byte byte) { return std::to_integer<std::uint8_t>(byte); });
    return {.pixels = std::move(pixels), .profile = std::move(profile)};
}
pipeline::Input surface_input(core::Budget& budget) {
    // This bright field reaches target 1 within the admitted default gain cap of 2.
    const auto bytes = make_gray_png({
        .width = 8,
        .height = 8,
        .depth = 8,
        .interlaced = false,
        .samples = std::vector<std::uint8_t>(64, 191),
    });
    pipeline::Input input;
    input.encoded().source = budget.allocate(bytes.size()).value();
    std::ranges::copy(std::as_bytes(std::span{bytes}), input.encoded().source.bytes().begin());
    input.encoded().source_identity = fs::identify(input.encoded().source.bytes()).value();
    return input;
}
methods::Plan surface_plan() {
    methods::Plan plan;
    plan.photometric.illumination = methods::Surface::create({.strength = 1, .target = 1}).value();
    plan.output = methods::ContinuousOutput{.depth = image::OutputDepth::byte};
    return plan;
}
void check_surface(const methods::SurfaceFacts& facts) {
    CHECK(facts.disposition == methods::Disposition::applied);
    CHECK(facts.counts.evaluated == 64);
    CHECK(facts.counts.changed == 64);
}
// Arm only on the real I01 measurement path. The next processing checkpoint is its eager
// application, after fitting/measurement, rather than the pipeline entry checkpoint.
class ApplicationStop {
  public:
    ApplicationStop() {
        measured.store(false);
        stopped.store(false);
    }
    static bool observe(core::Checkpoint at) noexcept {
        if (at == core::Checkpoint::measurement) {
            measured.store(true);
        }
        if (at == core::Checkpoint::processing && measured.load()) {
            stopped.store(true);
        }
        return stopped.load();
    }
    [[nodiscard]] static core::Cancellation cancellation() noexcept {
        return core::Cancellation{{}, observe};
    }
    inline static std::atomic<bool> measured{false}, stopped{false};
};
void check_surface_verification_stop(const pipeline::Failure& failure) {
    REQUIRE(CheckpointStop::stopped());
    REQUIRE(failure.completed.illumination.has_value());
    if (!failure.completed.illumination.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    check_surface(std::get<methods::SurfaceFacts>(failure.completed.illumination.value()));
    CHECK(!failure.failed_stage);
}
void check_surface_application_stop(const pipeline::Failure& failure) {
    REQUIRE(ApplicationStop::measured.load());
    REQUIRE(ApplicationStop::stopped.load());
    CHECK(failure.failed_stage == methods::Family::illumination);
    CHECK(!failure.completed.illumination);
}
void check_surface_cancellation_case(const TemporaryDirectory& directory, bool verification) {
    core::Budget budget{byte_budget};
    {
        const ApplicationStop application;
        const CheckpointStop stop{core::Checkpoint::verification, 0};
        const core::Cancellation cancellation =
            verification ? stop.cancellation() : ApplicationStop::cancellation();
        const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                        cancellation};
        const auto result =
            pipeline::render(surface_input(budget), surface_plan(), budget, scheduler);
        REQUIRE(!result);
        CHECK(result.error().error.code == core::ErrorCode::cancelled);
        const auto& failure = result.error();
        if (verification) {
            check_surface_verification_stop(failure);
        } else {
            check_surface_application_stop(failure);
        }
        CHECK(empty_directory(directory.path));
    }
    CHECK(budget.used() == 0);
}
} // namespace
TEST_CASE("Continuous output verification rejects wrong samples before publication",
          "[continuous][io]") {
    const TemporaryDirectory directory{"docenhance-color"};
    core::Budget budget{byte_budget};
    auto fixture = continuous_fixture(budget);
    const auto held = budget.used();
    {
        const auto encoded = codec::encode_png(fixture.image(), budget);
        REQUIRE(encoded);
        REQUIRE(codec::compare_png(encoded->bytes(), fixture.image(), budget));
        fixture.pixels.view().row(0).front() ^= std::uint8_t{1};
        const auto refused =
            publish_compared_png(utf8_spelling(directory.path / "output"),
                                 {.bytes = encoded->bytes(), .intended = fixture.image()}, budget);
        REQUIRE(!refused);
        CHECK(refused.error().error.code == core::ErrorCode::output_verify);
        CHECK(refused.error().publication == core::Publication::not_started);
        CHECK(empty_directory(directory.path));
    }
    CHECK(budget.used() == held);
}
TEST_CASE("Independent output verification checks metadata not just pixels", "[continuous][io]") {
    core::Budget budget{byte_budget};
    const auto fixture = continuous_fixture(budget);
    const auto encoded = codec::encode_png(fixture.image(), budget);
    REQUIRE(encoded);
    REQUIRE(codec::compare_png(encoded->bytes(), fixture.image(), budget));
    auto intended = fixture.image();
    intended.resolution = image::Resolution{.x = 1000, .y = 1000};
    const auto refused = codec::compare_png(encoded->bytes(), intended, budget);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::output_verify);
}
TEST_CASE("Cancellation during continuous verification never enters publication",
          "[continuous][cancellation]") {
    const TemporaryDirectory directory{"docenhance-color"};
    core::Budget budget{byte_budget};
    const auto fixture = continuous_fixture(budget);
    const auto held = budget.used();
    bool completed = false;
    constexpr std::size_t attempts = 256;
    for (std::size_t after = 0; after < attempts; ++after) {
        const CheckpointStop stop{core::Checkpoint::verification, after};
        const auto result = publish_png_fixture(utf8_spelling(directory.path / "output"),
                                                fixture.image(), budget, stop.cancellation());
        if (CheckpointStop::stopped()) {
            REQUIRE(!result);
            CHECK(result.error().error.code == core::ErrorCode::cancelled);
            CHECK(result.error().publication == core::Publication::not_started);
            CHECK(empty_directory(directory.path));
        } else {
            REQUIRE(result);
            CHECK(after > 1);
            completed = true;
        }
        CHECK(budget.used() == held);
        if (completed) {
            break;
        }
    }
    REQUIRE(completed);
}
TEST_CASE("I01 failed independent comparison retains completed stage accounting and refunds",
          "[surface][io]") {
    const TemporaryDirectory directory{"docenhance-color"};
    core::Budget budget{byte_budget};
    auto fixture = continuous_fixture(budget);
    std::ranges::fill(fixture.pixels.view().storage(), std::uint8_t{255});
    const auto held = budget.used();
    {
        const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
        const auto rendered =
            pipeline::render(surface_input(budget), surface_plan(), budget, scheduler);
        REQUIRE(rendered);
        REQUIRE(rendered->stages.illumination.has_value());
        if (!rendered->stages.illumination.has_value()) {
            throw std::logic_error("Required test observation is absent");
        }
        REQUIRE(rendered->output.decoded_and_compared);
        const auto& facts = std::get<methods::SurfaceFacts>(rendered->stages.illumination.value());
        check_surface(facts);
        REQUIRE(codec::compare_png(rendered->result.bytes(), fixture.image(), budget));
        fixture.pixels.view().row(0).front() = 254;
        const auto refused = publish_compared_png(
            utf8_spelling(directory.path / "output"),
            {.bytes = rendered->result.bytes(), .intended = fixture.image()}, budget);
        REQUIRE(!refused);
        CHECK(refused.error().error.code == core::ErrorCode::output_verify);
        CHECK(refused.error().publication == core::Publication::not_started);
        check_surface(facts);
        CHECK(empty_directory(directory.path));
    }
    CHECK(budget.used() == held);
}
TEST_CASE("I01 eager application versus verification cancellation preserves stage truth",
          "[surface][cancellation][io]") {
    const TemporaryDirectory directory{"docenhance-color"};
    for (const bool verification : {false, true}) {
        check_surface_cancellation_case(directory, verification);
    }
}
TEST_CASE("Protection decoding accounts allocations and refunds every partial refusal",
          "[surface][mask][resources]") {
    const auto bytes = make_gray_png({
        .width = 8,
        .height = 8,
        .depth = 1,
        .interlaced = true,
        .samples = std::vector<std::uint8_t>(64, 1),
    });
    for (const auto limit : {std::size_t{0}, std::size_t{64}, std::size_t{4096}, byte_budget}) {
        core::Budget budget{limit};
        {
            auto result = codec::decode_protection_png(std::as_bytes(std::span{bytes}),
                                                       {.width = 8, .height = 8}, budget);
            if (result) {
                CHECK(result->view().row(0).front() == 1);
            } else {
                CHECK(result.error().code == core::ErrorCode::resource);
            }
            CHECK((limit != byte_budget || result.has_value()));
        }
        CHECK(budget.used() == 0);
    }
}
} // namespace docenhance::tests
