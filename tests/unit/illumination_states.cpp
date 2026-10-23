// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/codec/protection_png.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/surface_working.hpp"
#include "png_fixture.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace docenhance::tests {
namespace {
app::Invocation invocation() {
    app::Invocation request;
    request.command = app::Command::process;
    request.operands = {"input.png"};
    request.option(app::OptionId::out_dir) = "output";
    return request;
}
void check_healthy_protection(const image::Plane<std::uint8_t>& decoded, std::uint32_t width,
                              std::size_t allowed) {
    CHECK(decoded.width() == width);
    CHECK(std::ranges::all_of(decoded.view().row(0), [](auto value) { return value == 1; }));
    CHECK(CheckpointStop::visits() == allowed);
}
bool check_protection_decode(const core::Result<image::Plane<std::uint8_t>>& decoded,
                             std::uint32_t width, std::size_t allowed, std::size_t& cancellations) {
    if (decoded) {
        check_healthy_protection(*decoded, width, allowed);
        return true;
    }
    ++cancellations;
    CHECK(decoded.error().code == core::ErrorCode::cancelled);
    CHECK(CheckpointStop::visits() == allowed + 1);
    // The actual decoder failure precedes every method; the real host prefix
    // witness in stage_outcomes.cpp checks absent failed_stage/completed Facts.
    return false;
}
bool protection_decode_attempt(std::span<const std::uint8_t> bytes, std::uint32_t width,
                               std::size_t allowed, std::size_t& cancellations) {
    bool completed = false;
    core::Budget budget{std::size_t{4} * 1024 * 1024};
    const CheckpointStop stop{core::Checkpoint::decode, allowed};
    {
        const auto decoded = codec::decode_protection_png(std::as_bytes(std::span{bytes}),
                                                          {.width = width, .height = 1}, budget,
                                                          stop.cancellation());
        completed = check_protection_decode(decoded, width, allowed, cancellations);
    }
    CHECK(budget.used() == 0);
    return completed;
}
} // namespace
TEST_CASE("Surface completed identity reasons retain their actual decision priority",
          "[app][surface]") {
    for (unsigned combination = 0; combination < 3; ++combination) {
        auto raw = invocation();
        raw.option(app::OptionId::illumination) = "surface";
        raw.option(app::OptionId::background_strength) = combination < 2 ? "0" : "1";
        raw.option(app::OptionId::background_max_gain) = "1";
        if (combination == 0) {
            raw.option(app::OptionId::protect_mask) = "mask.png";
        }
        const auto request = app::prepare_process(raw);
        REQUIRE(request);
        core::Budget budget{std::size_t{1024} * 1024};
        auto luminance = image::Plane<double>::allocate(budget, 32, 32);
        auto mask = image::Plane<std::uint8_t>::allocate(budget, 32, 32);
        REQUIRE(luminance);
        REQUIRE(mask);
        std::ranges::fill(luminance->view().storage(), 0.5);
        std::ranges::fill(mask->view().storage(), std::uint8_t{1});
        const auto resident = budget.used();
        const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
        REQUIRE(concurrency);
        const exec::Scheduler scheduler{*concurrency};
        const auto& method = std::get<methods::Surface>(request->plan().photometric.illumination);
        const auto facts =
            methods::apply(method, {luminance->view(),
                                    combination == 0 ? mask->view().as_const()
                                                     : image::PlaneView<const std::uint8_t>{},
                                    budget, scheduler});
        REQUIRE(facts);
        CHECK(facts->reason == (combination == 0   ? methods::SurfaceReason::no_eligible_samples
                                : combination == 1 ? methods::SurfaceReason::zero_strength
                                                   : methods::SurfaceReason::unit_gain));
        CHECK(facts->counts.evaluated == 0);
        CHECK(facts->counts.changed == 0);
        CHECK(budget.used() == resident);
        CHECK(luminance->view().row(0).front() == 0.5);
        CHECK(luminance->view().row(31).back() == 0.5);
        CHECK(methods::validate(*facts, method.parameters(), {.width = 32, .height = 32}));
        auto contradicted = *facts;
        contradicted.reason = combination == 1 ? methods::SurfaceReason::unit_gain
                                               : methods::SurfaceReason::zero_strength;
        CHECK(!methods::validate(contradicted, method.parameters(), {.width = 32, .height = 32}));
    }
}
TEST_CASE(
    "Disabled illumination cannot become failed work during real protection decode cancellation",
    "[app][codec]") {
    constexpr std::uint32_t width = 3 * 4096;
    const auto bytes = make_gray_png({
        .width = width,
        .height = 1,
        .depth = 8,
        .interlaced = false,
        .samples = std::vector<std::uint8_t>(width, 1),
    });
    const std::vector<std::uint8_t> original(bytes.begin(), bytes.end());
    auto raw = invocation();
    raw.option(app::OptionId::protect_mask) = "mask.png";
    const auto request = app::prepare_process(raw);
    REQUIRE(request);
    bool completed = false;
    std::size_t cancellations = 0;
    for (std::size_t allowed = 0; allowed < 512; ++allowed) {
        completed = protection_decode_attempt(bytes, width, allowed, cancellations);
        CHECK(bytes == original);
        if (completed) {
            break;
        }
    }
    REQUIRE(completed);
    CHECK(cancellations > 3);
}
} // namespace docenhance::tests
