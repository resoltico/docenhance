// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/nlm_working.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/pipeline/render.hpp"
#include "gamma_fixture.hpp"
#include "png_fixture.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>
namespace docenhance::tests {
namespace {
std::vector<std::uint8_t> linear_png() {
    const auto source = make_gray_png({
        .width = 7,
        .height = 7,
        .depth = 8,
        .interlaced = false,
        .samples = std::vector<std::uint8_t>(49, 97),
    });
    std::vector<std::uint8_t> png(source.begin(), source.begin() + 33);
    std::vector<std::uint8_t> gamma;
    append_be32(gamma, 100000);
    append_chunk(png, "gAMA", gamma);
    png.insert(png.end(), source.begin() + 33, source.end());
    return png;
}
pipeline::Input denoising_input(core::Budget& budget) {
    const auto png = linear_png();
    auto protection = std::vector<std::uint8_t>(49, 0);
    protection.front() = 1;
    const auto mask = make_gray_png({
        .width = 7,
        .height = 7,
        .depth = 1,
        .interlaced = false,
        .samples = std::move(protection),
    });
    pipeline::Input input;
    input.encoded().source = budget.allocate(png.size()).value();
    input.encoded().mask = budget.allocate(mask.size()).value();
    std::ranges::copy(std::as_bytes(std::span{png}), input.encoded().source.bytes().begin());
    std::ranges::copy(std::as_bytes(std::span{mask}), input.encoded().mask.bytes().begin());
    input.encoded().source_identity = fs::identify(input.encoded().source.bytes()).value();
    input.encoded().mask_identity = fs::identify(input.encoded().mask.bytes()).value();
    return input;
}
void check_no_correction_facts(const pipeline::Rendering& filtered) {
    REQUIRE(filtered.stages.denoising.has_value());
    if (!filtered.stages.denoising.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    const auto& facts = std::get<methods::NlmFacts>(filtered.stages.denoising.value());
    CHECK(facts.counts.protected_samples == 1);
    CHECK(facts.counts.evaluated == 48);
    CHECK(facts.corrected_samples == 0);
    CHECK(facts.counts.changed == 0);
}
void check_no_correction_output(const pipeline::Rendering& baseline,
                                const pipeline::Rendering& filtered, unsigned mode) {
    CHECK(baseline.output.decoded_and_compared);
    CHECK(filtered.output.decoded_and_compared);
    CHECK(filtered.output.shape.depth.bits() == (mode == 0 ? 8 : mode == 1 ? 16 : 1));
}
void check_no_correction_binary(const pipeline::Rendering& filtered) {
    REQUIRE(filtered.stages.binarization.has_value());
    if (!filtered.stages.binarization.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    const auto& binary = std::get<methods::FixedFacts>(filtered.stages.binarization.value());
    CHECK(binary.black_samples == 0);
    CHECK(binary.white_samples == 49);
}
void check_no_correction_assets(const pipeline::Rendering& baseline,
                                const pipeline::Rendering& filtered) {
    CHECK(std::ranges::equal(baseline.result.bytes(), filtered.result.bytes()));
    REQUIRE(baseline.mask);
    if (!baseline.mask.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    REQUIRE(filtered.mask);
    if (!filtered.mask.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    CHECK(std::ranges::equal(baseline.mask.value().bytes(), filtered.mask.value().bytes()));
}
void check_no_correction_mode(unsigned mode) {
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    {
        methods::Plan plan;
        plan.protection = true;
        if (mode == 2) {
            plan.output =
                methods::BinaryOutput{.method = methods::FixedThreshold::create(0.5).value()};
        } else {
            plan.output = methods::ContinuousOutput{
                .depth = mode == 0 ? image::OutputDepth::byte : image::OutputDepth::word,
            };
        }
        const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
        const auto baseline = pipeline::render(denoising_input(budget), plan, budget, scheduler);
        REQUIRE(baseline);
        plan.photometric.denoising =
            methods::Nlm::create({.h = 0.1, .patch = 3, .search = 7}).value();
        const auto filtered = pipeline::render(denoising_input(budget), plan, budget, scheduler);
        REQUIRE(filtered);
        check_no_correction_facts(*filtered);
        check_no_correction_output(*baseline, *filtered, mode);
        if (mode == 2) {
            check_no_correction_binary(*filtered);
        }
        check_no_correction_assets(*baseline, *filtered);
    }
    CHECK(budget.used() == 0);
}
} // namespace
TEST_CASE("Eager D01 protected and zero-correction samples retain exact entering Y and padding") {
    core::Budget budget{std::size_t{1024} * 1024};
    {
        auto luminance = image::Plane<double>::allocate(budget, 7, 7).value();
        auto mask = image::Plane<std::uint8_t>::allocate(budget, 7, 7).value();
        std::ranges::fill(luminance.view().storage(), -2);
        std::ranges::fill(mask.view().storage(), 255);
        constexpr double entering = 0.123456789;
        for (std::uint32_t y = 0; y < 7; ++y) {
            std::ranges::fill(luminance.view().row(y), entering);
            std::ranges::fill(mask.view().row(y), 0);
        }
        mask.view().row(0).front() = 1;
        luminance.view().row(0).front() = 0.123456790;
        const auto code = std::floor((65535 * image::srgb_encode(entering)) + 0.5);
        REQUIRE(image::srgb_decode(code / 65535) != entering);
        REQUIRE(std::floor((65535 * image::srgb_encode(luminance.view().row(0).front())) + 0.5) ==
                code);
        const auto before = gamma_bits(luminance.view().storage());
        const auto held = budget.used();
        const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
        const auto method = methods::Nlm::create({.h = 0.1, .patch = 3, .search = 7}).value();
        const auto facts =
            methods::apply(method, {luminance.view(), mask.view().as_const(), budget, scheduler});
        REQUIRE(facts);
        CHECK(facts->counts.protected_samples == 1);
        CHECK(facts->counts.evaluated == 48);
        CHECK(facts->corrected_samples == 0);
        CHECK(facts->counts.changed == 0);
        CHECK(gamma_bits(luminance.view().storage()) == before);
        CHECK(budget.used() == held);
        const auto retained = *facts;
        const auto repeated =
            methods::apply(method, {luminance.view(), mask.view().as_const(), budget, scheduler});
        REQUIRE(repeated);
        CHECK(*repeated == retained);
        CHECK(*facts == retained);
        CHECK(gamma_bits(luminance.view().storage()) == before);
        CHECK(budget.used() == held);
    }
    CHECK(budget.used() == 0);
}
TEST_CASE("Real NLM no-correction reconstruction keeps no-filter continuous and BW final bytes") {
    constexpr double entering = 97.0 / 255;
    const auto code = std::floor((65535 * image::srgb_encode(entering)) + 0.5);
    REQUIRE(image::srgb_decode(code / 65535) != entering);
    for (const unsigned mode : {0U, 1U, 2U}) {
        check_no_correction_mode(mode);
    }
}
} // namespace docenhance::tests
