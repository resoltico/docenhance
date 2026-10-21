// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/png_artifact.hpp"
#include "docenhance/codec/png_output.hpp"
#include "docenhance/color/profile.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/pipeline/render.hpp"
#include "png_fixture.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
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
constexpr std::uint32_t width = 32;
constexpr std::uint32_t height = 32;
std::vector<std::uint8_t> alpha_source() {
    std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<std::uint8_t> header;
    append_be32(header, width);
    append_be32(header, height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    append_chunk(png, "IHDR", header);
    std::vector<std::uint8_t> raw;
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), std::size_t{width} * 4, 128);
    }
    append_chunk(png, "IDAT", stored_deflate(raw));
    append_chunk(png, "IEND", {});
    return png;
}
pipeline::Input illumination_input(core::Budget& budget) {
    const auto bytes = alpha_source();
    std::vector<std::uint8_t> protection(std::size_t{width} * height, 0);
    protection.back() = 1;
    const auto mask = make_gray_png(
        {.width = width, .height = height, .depth = 8, .interlaced = false, .samples = protection});
    pipeline::Input input;
    auto source_storage = budget.allocate(bytes.size());
    auto mask_storage = budget.allocate(mask.size());
    REQUIRE(source_storage);
    REQUIRE(mask_storage);
    std::ranges::copy(std::as_bytes(std::span{bytes}), source_storage->bytes().begin());
    std::ranges::copy(std::as_bytes(std::span{mask}), mask_storage->bytes().begin());
    const auto source_id = fs::identify(source_storage->bytes());
    const auto mask_id = fs::identify(mask_storage->bytes());
    REQUIRE(source_id);
    REQUIRE(mask_id);
    input.encoded().source = std::move(*source_storage);
    input.encoded().source_identity = *source_id;
    input.encoded().mask = std::move(*mask_storage);
    input.encoded().mask_identity = *mask_id;
    return input;
}
void configure_illumination_plan(methods::Plan& plan, bool morphological, unsigned mode) {
    plan.protection = true;
    if (morphological) {
        const auto method = methods::Morphology::create({.target = 1, .radius = 1});
        REQUIRE(method);
        plan.photometric.illumination = *method;
    } else {
        const auto method = methods::Surface::create({.target = 1, .cell = 8});
        REQUIRE(method);
        plan.photometric.illumination = *method;
    }
    if (mode == 2) {
        const auto fixed = methods::FixedThreshold::create(0.9);
        REQUIRE(fixed);
        plan.output = methods::BinaryOutput{.method = *fixed};
    } else {
        plan.output = methods::ContinuousOutput{
            .mode = mode == 0 ? image::ToneMode::preserve : image::ToneMode::gray,
        };
    }
}
pipeline::Rendering illuminate(core::Budget& budget, bool morphological, unsigned mode) {
    auto input = illumination_input(budget);
    methods::Plan plan;
    configure_illumination_plan(plan, morphological, mode);
    const auto concurrency = exec::Concurrency::resolve(2, 2, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    auto rendered = pipeline::render(std::move(input), plan, budget, scheduler);
    REQUIRE(rendered);
    REQUIRE(rendered->output.decoded_and_compared);
    // NOLINTNEXTLINE(bugprone-use-after-move)
    CHECK(!input.ready());
    return std::move(*rendered);
}
image::Plane<std::uint8_t> comparison_pixels(core::Budget& budget, unsigned mode) {
    const unsigned channels = mode == 0 ? 3 : 1;
    auto pixels = image::Plane<std::uint8_t>::allocate(budget, width * channels, height);
    REQUIRE(pixels);
    auto owned = std::move(*pixels);
    for (std::uint32_t y = 0; y < height; ++y) {
        std::ranges::fill(owned.view().row(y), mode == 2 ? 1 : 255);
    }
    const double opacity = 128.0 / 255;
    const double linear = std::pow((opacity + 0.055) / 1.055, 2.4);
    const double composed = (opacity * linear) + (1 - opacity);
    const double encoded = (1.055 * std::pow(composed, 1.0 / 2.4)) - 0.055;
    const auto protected_code = static_cast<std::uint8_t>(std::floor((255 * encoded) + 0.5));
    const auto last = owned.view().row(height - 1).last(channels);
    std::ranges::fill(last, mode == 2 ? 0 : protected_code);
    return owned;
}
image::Plane<std::uint8_t> comparison_profile(core::Budget& budget, unsigned mode) {
    image::Plane<std::uint8_t> profile;
    if (mode != 2) {
        auto canonical = color::make_output_profile(budget, mode == 1);
        REQUIRE(canonical);
        auto storage = image::Plane<std::uint8_t>::allocate(
            budget, static_cast<std::uint32_t>(canonical->size()), 1);
        REQUIRE(storage);
        std::ranges::transform(canonical->bytes(), storage->view().row(0).begin(),
                               [](std::byte value) { return static_cast<std::uint8_t>(value); });
        profile = std::move(*storage);
    }
    return profile;
}
void check_rendered_shape(const pipeline::OutputFacts& output, unsigned mode) {
    REQUIRE(output.shape.width == width);
    REQUIRE(output.shape.height == height);
    CHECK(!output.resolution);
    const unsigned channels = mode == 0 ? 3 : 1;
    CHECK(image::components(output.shape.model) == channels);
    CHECK(output.shape.depth.bits() == (mode == 2 ? 1U : 8U));
}
void compare_output(pipeline::Rendering& rendered, core::Budget& budget, unsigned mode) {
    check_rendered_shape(rendered.output, mode);
    auto pixels = comparison_pixels(budget, mode);
    auto profile = comparison_profile(budget, mode);
    const auto completed = rendered.stages;
    const auto interpretation = rendered.interpretation;
    const codec::PngImage intended{
        .shape = rendered.output.shape,
        .pixels = pixels.view().as_const(),
        .profile =
            profile.empty() ? std::span<const std::byte>{} : std::as_bytes(profile.view().row(0)),
        .resolution = std::nullopt,
    };
    REQUIRE(codec::compare_png(rendered.result.bytes(), intended, budget));
    const auto observed = codec::observe_png_artifact(rendered.result.bytes(), budget, {});
    REQUIRE(observed);
    CHECK(rendered.stages == completed);
    CHECK(rendered.interpretation == interpretation);
}
void check_illumination_counts(const methods::PixelCounts& counts, double min_gain,
                               double max_gain) {
    CHECK(counts.eligible == (std::uint64_t{width} * height) - 1);
    CHECK(counts.protected_samples == 1);
    CHECK(counts.evaluated == (std::uint64_t{width} * height) - 1);
    CHECK(counts.changed == (std::uint64_t{width} * height) - 1);
    CHECK(min_gain > 1.5);
    CHECK(max_gain < 2);
}
void check_illumination_rendering(core::Budget& budget, bool morphological, unsigned mode) {
    auto rendered = illuminate(budget, morphological, mode);
    REQUIRE(rendered.stages.illumination.has_value());
    if (!rendered.stages.illumination.has_value()) {
        throw std::logic_error("Required completed illumination is absent");
    }
    std::visit(
        [](const auto& facts) {
            check_illumination_counts(facts.counts, facts.min_gain, facts.max_gain);
        },
        rendered.stages.illumination.value());
    CHECK(rendered.interpretation.flattened_pixels == std::uint64_t{width} * height);
    REQUIRE(rendered.mask);
    if (!rendered.mask.has_value()) {
        throw std::logic_error("Required rendered protection mask is absent");
    }
    const auto mask = codec::observe_png_artifact(rendered.mask.value().bytes(), budget, {});
    REQUIRE(mask);
    CHECK(mask->protected_samples == 1);
    compare_output(rendered, budget, mode);
}
void illumination_mode_case(bool morphological, unsigned mode) {
    core::Budget budget{std::size_t{32} * 1024 * 1024};
    check_illumination_rendering(budget, morphological, mode);
    CHECK(budget.used() == 0);
}
} // namespace
TEST_CASE("Eager illumination completes once and comparison cannot replay observations",
          "[pipeline]") {
    for (const bool morphological : {false, true}) {
        for (const unsigned mode : {0U, 1U, 2U}) {
            illumination_mode_case(morphological, mode);
        }
    }
}
} // namespace docenhance::tests
