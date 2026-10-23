// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/restoration_working.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/codec/psf_png.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fs/snapshot.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "gamma_fixture.hpp"
#include "png_fixture.hpp"
#include "require.hpp"
#include "restoration_working_detail.hpp"
#include "restoration_working_fixture.hpp"
#include "restoration_working_reference.hpp"
#include "temporary_directory.hpp"

#include <algorithm>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>
namespace docenhance::tests {
namespace {
auto run_restoration(RestorationWorkingFixture& f, const methods::Wiener& method,
                     const methods::ResolvedPsf& psf, const exec::Scheduler& scheduler) {
    return methods::apply(
        method, psf, {f.luminance.view(), f.protection.view().as_const(), f.budget, scheduler});
}
void check_protected_backing(RestorationWorkingFixture& fixture,
                             const std::vector<std::uint64_t>& before) {
    const auto storage = fixture.luminance.view().storage();
    const auto pitch = fixture.luminance.view().row_pitch();
    for (std::size_t i = 0; i < storage.size(); ++i) {
        const auto y = i / pitch;
        const auto x = i % pitch;
        if (x >= fixture.luminance.width() ||
            fixture.protection.view().row(static_cast<std::uint32_t>(y)).subspan(x, 1).front() !=
                0) {
            require(std::bit_cast<std::uint64_t>(storage.subspan(i, 1).front()) == before.at(i),
                    "actual large protected band and all Y padding retain exact entering bits");
        }
    }
}
void check_reference(RestorationWorkingFixture& fixture, const methods::Wiener& method,
                     const methods::ResolvedPsf& psf) {
    const auto reference = restoration_working_reference(fixture, method, psf);
    require(reference.minimum_target_margin > 1e-8,
            "independent nonconstant fixture attains robust correction decisions");
    const auto original = gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> mask_before(mask.begin(), mask.end());
    const auto held = fixture.budget.used();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(2, 2, 0, 0).value()};
    const auto result = run_restoration(fixture, method, psf, scheduler);
    require(result && methods::validate(*result, method.parameters(),
                                        {
                                            .width = fixture.luminance.width(),
                                            .height = fixture.luminance.height(),
                                        }),
            "actual Working R01 completes typed parameter/canvas validation");
    require(result->counts == reference.counts &&
                result->corrected_samples == reference.corrected_samples &&
                result->raw_low_samples == reference.raw_low_samples &&
                result->raw_high_samples == reference.raw_high_samples &&
                result->blended_low_samples == reference.blended_low_samples &&
                result->blended_high_samples == reference.blended_high_samples &&
                result->context_samples == reference.context_samples,
            "actual counts match independent full-complex direct DFT/filter/crop/blend on "
            "separated fixture");
    require(
        result->transform && result->transform->guard == reference.transform.guard &&
            result->transform->width == reference.transform.width &&
            result->transform->height == reference.transform.height &&
            std::abs(result->transform->padded_mean - reference.transform.padded_mean) < 1e-14,
        "actual reflected padded geometry and compensated mean agree with independent definition");
    const auto output = fixture.luminance.view().storage();
    for (std::size_t i = 0; i < output.size(); ++i) {
        if (std::bit_cast<std::uint64_t>(reference.output.at(i)) == original.at(i)) {
            require(std::bit_cast<std::uint64_t>(output.subspan(i, 1).front()) == original.at(i),
                    "protected and padding backing retain exact entering bits");
        } else {
            require(std::abs(output.subspan(i, 1).front() - reference.output.at(i)) < 1e-12,
                    "actual binary64 restoration agrees with independent direct Fourier output");
        }
    }
    require(
        fixture.budget.used() == held && std::ranges::equal(mask, mask_before),
        "actual spectra/native leases/mean/observations refund and all mask bytes remain exact");
}
void check_wide_mean(std::uint32_t width, std::size_t extra = restoration_test_scratch) {
    const auto value = image::srgb_decode(235.0 / 255.0);
    const auto psf = restoration_gaussian();
    for (const auto workers : {1U, 2U, 7U}) {
        RestorationWorkingFixture fixture{width, 1, extra};
        std::ranges::fill(fixture.luminance.view().row(0), value);
        const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
        const methods::Working working{fixture.luminance.view(),
                                       fixture.protection.view().as_const(), fixture.budget,
                                       scheduler};
        const auto shape = methods::restoration_geometry(working, psf).value();
        auto frame =
            image::Plane<double>::allocate(fixture.budget, shape.width, shape.height).value();
        const auto held = fixture.budget.used();
        const auto mean = methods::restoration_mean(working, frame.view(), shape);
        require(mean && std::abs(*mean - value) <= 1e-15 && fixture.budget.used() == held &&
                    frame.view().row(0).front() == value &&
                    frame.view().row(shape.height - 1).back() == value,
                "actual reflected gather uses both compensated levels and refunds row sums");
        std::cout << "{\"mean_width\":" << width << ",\"configured_workers\":" << workers
                  << ",\"extra_capacity\":" << extra
                  << ",\"frame_bytes\":" << frame.view().storage().size_bytes()
                  << ",\"charged_peak\":" << fixture.budget.peak() << "}\n";
    }
}
methods::ResolvedPsf decoded_shift_psf(const std::filesystem::path& path, core::Budget& budget) {
    const auto bytes = make_gray_png({
        .width = 3,
        .height = 3,
        .depth = 8,
        .interlaced = false,
        .samples = {0, 0, 0, 0, 0, 255, 0, 0, 0},
    });
    std::ofstream stream{path, std::ios::binary};
    for (const auto byte : bytes) {
        stream.put(static_cast<char>(byte));
    }
    stream.close();
    require(static_cast<bool>(stream), "real asymmetric kernel fixture is closed before admission");
    const auto name = utf8_spelling(path);
    auto snapshot = fs::read_source_snapshot(name, budget, {}, core::psf_encoded_bytes_max).value();
    const auto loaded = codec::decode_psf_png(snapshot.encoded.bytes(), budget).value();
    methods::ResolvedPsf psf{
        .width = loaded.shape.width,
        .height = loaded.shape.height,
        .coefficients = {},
        .source_identity = std::move(snapshot.identity),
    };
    const auto pixels = loaded.pixels.view();
    for (std::uint32_t y = 0; y < psf.height; ++y) {
        const auto row = pixels.row(y);
        for (const auto sample : row) {
            psf.coefficients.push_back(sample);
        }
    }
    methods::normalize_psf(psf.width, psf.height, psf.coefficients).value();
    const auto centroid = methods::psf_centroid(psf.width, psf.height, psf.coefficients).value();
    require(centroid.front() == 1 && centroid.back() == 0,
            "actual decoded positive-x impulse retains its central-displacement moment");
    psf.centroid_x = centroid.front();
    psf.centroid_y = centroid.back();
    return psf;
}
} // namespace
TEST_CASE("Working restoration agrees with independent complex DFT reflected crop and counts") {
    const auto assertions_before = assertion_counter().load();
    RestorationWorkingFixture fixture{3, 2};
    fixture.protection.view().row(0).front() = 1;
    const auto method = methods::Wiener::create({.k = 0.1}).value();
    const auto psf = restoration_gaussian();
    check_reference(fixture, method, psf);
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE(
    "Working restoration validates PSF before zero blend and eligibility without FFT scratch") {
    const auto assertions_before = assertion_counter().load();
    RestorationWorkingFixture fixture{1, 1, 0};
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto resident = fixture.budget.used();
    const auto psf = restoration_gaussian();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto zero = methods::Wiener::create({.blend = 0}).value();
    const auto result = run_restoration(fixture, zero, psf, scheduler);
    require(result && result->reason == methods::WienerReason::zero_blend && !result->transform &&
                result->context_samples == 0 && fixture.budget.used() == resident &&
                fixture.budget.peak() == resident &&
                gamma_bits(fixture.luminance.view().storage()) == before,
            "actual singleton zero blend performs no FFT work or stores");
    std::ranges::fill(fixture.protection.view().row(0), std::uint8_t{1});
    const auto active = methods::Wiener::create().value();
    const auto empty = run_restoration(fixture, active, psf, scheduler);
    require(empty && empty->reason == methods::WienerReason::no_eligible_samples &&
                !empty->transform && fixture.budget.used() == resident &&
                fixture.budget.peak() == resident &&
                gamma_bits(fixture.luminance.view().storage()) == before,
            "actual E0 admits larger PSF and preserves all Y bits without algorithm allocation");
    auto invalid = psf;
    invalid.coefficients.front() = -1;
    const auto refused = run_restoration(fixture, zero, invalid, scheduler);
    require(!refused && refused.error().code == core::ErrorCode::argument &&
                fixture.budget.used() == resident,
            "invalid actual public PSF values refuse before either bypass");
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working restoration consumes protected comparison context before Fourier work") {
    const auto assertions_before = assertion_counter().load();
    RestorationWorkingFixture fixture{3, 2};
    fixture.protection.view().row(0).front() = 1;
    fixture.luminance.view().row(0).front() = std::numeric_limits<double>::quiet_NaN();
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto held = fixture.budget.used();
    const auto method = methods::Wiener::create().value();
    const CheckpointStop trace{core::Checkpoint::solving, std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler scheduler{
        exec::Concurrency::resolve(1, 1, 0, 0).value(),
        trace.cancellation(),
    };
    const auto refused = run_restoration(fixture, method, restoration_gaussian(), scheduler);
    require(!refused && refused.error().code == core::ErrorCode::argument &&
                fixture.budget.used() == held && CheckpointStop::visits() == 0 &&
                gamma_bits(fixture.luminance.view().storage()) == before,
            "actual active reflected gather rejects protected-only poison and refunds without Y "
            "mapping");
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working restoration fixed bands give exact one two seven worker output and facts") {
    const auto assertions_before = assertion_counter().load();
    constexpr std::uint32_t width = 768;
    constexpr std::uint32_t height = 449;
    const auto method = methods::Wiener::create().value();
    const auto psf = restoration_gaussian();
    std::optional<methods::WienerFacts> expected;
    std::vector<std::uint64_t> expected_bits;
    for (const auto workers : {1U, 2U, 7U}) {
        RestorationWorkingFixture fixture{width, height};
        for (std::uint32_t y = 64; y < 128; ++y) {
            std::ranges::fill(fixture.protection.view().row(y), std::uint8_t{1});
        }
        const auto held = fixture.budget.used();
        const auto before = gamma_bits(fixture.luminance.view().storage());
        const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
        const auto result = run_restoration(fixture, method, psf, scheduler);
        require(result &&
                    methods::validate(*result, method.parameters(),
                                      {.width = width, .height = height}) &&
                    result->counts.changed > 0 && result->corrected_samples > 0 &&
                    scheduler.workers() == workers && fixture.budget.used() == held,
                "actual nonconstant restoration completes enough real row/column/map tasks and "
                "refunds");
        check_protected_backing(fixture, before);
        const auto bits = gamma_bits(fixture.luminance.view().storage());
        if (expected) {
            {
                require(*expected == *result && expected_bits == bits,
                        "actual1/2/7 partitions retain identical complete facts and Y backing");
            }
        } else {
            expected = *result;
            expected_bits = bits;
        }
    }
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working restoration preserves binary64 constants and endpoints across regularizers") {
    const auto assertions_before = assertion_counter().load();
    const auto psf = restoration_gaussian();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(2, 2, 0, 0).value()};
    for (const auto value : {0.0, 0.5, 1.0, 235.0 / 255.0}) {
        for (const auto k : {1e-5, 0.1, 1.0}) {
            RestorationWorkingFixture fixture{3, 2};
            for (std::uint32_t y = 0; y < 2; ++y) {
                std::ranges::fill(fixture.luminance.view().row(y), value);
            }
            fixture.protection.view().row(0).front() = 1;
            const auto before = gamma_bits(fixture.luminance.view().storage());
            const auto held = fixture.budget.used();
            const auto method = methods::Wiener::create({.k = k, .blend = 1}).value();
            const auto result = run_restoration(fixture, method, psf, scheduler);
            require(result && result->transform && result->context_samples == 6 &&
                        result->counts.evaluated == 5 && fixture.budget.used() == held,
                    "actual constant restoration traverses FFT and refunds scratch");
            require(std::abs(fixture.luminance.view().row(1).front() - value) <= 1e-12,
                    "actual binary64 DC preservation retains the unchanged constant bound");
            check_protected_backing(fixture, before);
        }
    }
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE(
    "Working restoration tiny blend counts actual unclamped targets rather than raw changes") {
    const auto assertions_before = assertion_counter().load();
    RestorationWorkingFixture fixture{2, 2};
    for (std::uint32_t y = 0; y < 2; ++y) {
        fixture.luminance.view().row(y).front() = 0;
        fixture.protection.view().row(y).front() = 1;
        fixture.luminance.view().row(y).back() = 1;
    }
    const auto before = gamma_bits(fixture.luminance.view().storage());
    const auto held = fixture.budget.used();
    const auto method =
        methods::Wiener::create({
                                    .k = 1e-5,
                                    .blend = std::numeric_limits<double>::denorm_min(),
                                })
            .value();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto result = run_restoration(fixture, method, restoration_gaussian(), scheduler);
    require(result && result->transform && result->counts.evaluated == 2 &&
                result->raw_high_samples == 2 && result->corrected_samples == 0 &&
                result->blended_high_samples == 0 && result->counts.changed == 0 &&
                fixture.budget.used() == held &&
                gamma_bits(fixture.luminance.view().storage()) == before,
            "actual raw excursions coexist with rounded-equal targets and no stores");
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE(
    "Working restoration reports tap-derived mass and centroid rather than stored summaries") {
    const auto assertions_before = assertion_counter().load();
    RestorationWorkingFixture fixture{1, 1, 0};
    auto psf = restoration_gaussian();
    const auto centroid = methods::psf_centroid(psf.width, psf.height, psf.coefficients).value();
    psf.centroid_x += methods::psf_normalization_tolerance / 2;
    psf.centroid_y -= methods::psf_normalization_tolerance / 2;
    double sum = 0;
    for (const auto coefficient : psf.coefficients) {
        sum += coefficient;
    }
    const auto method = methods::Wiener::create({.blend = 0}).value();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto result = run_restoration(fixture, method, psf, scheduler);
    require(result && !result->transform && result->psf.normalized_sum == sum &&
                result->psf.centroid_x == centroid.front() &&
                result->psf.centroid_y == centroid.back() &&
                result->psf.centroid_x != psf.centroid_x &&
                result->psf.centroid_y != psf.centroid_y,
            "actual production facts recompute admitted taps before zero-blend bypass");
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Working restoration compensated padded reduction preserves a wide gray constant") {
    const auto assertions_before = assertion_counter().load();
    check_wide_mean(65536);
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE(
    "Working restoration compensated padded reduction preserves a million column gray constant") {
    const auto assertions_before = assertion_counter().load();
    constexpr std::size_t extra = std::size_t{1024} * 1024 * 1024;
    check_wide_mean(1000000, extra);
    REQUIRE(assertion_counter().load() > assertions_before);
}

TEST_CASE("Working restoration asymmetric PSF phase agrees with independent Fourier crop") {
    const auto assertions_before = assertion_counter().load();
    RestorationWorkingFixture fixture{3, 2};
    fixture.protection.view().row(0).front() = 1;
    const TemporaryDirectory directory{"restoration-phase"};
    const auto path = directory.path / "phase-kernel.png";
    const auto psf = decoded_shift_psf(path, fixture.budget);
    const auto method = methods::Wiener::create({
                                                    .psf = methods::Kernel{},
                                                    .k = 0.1,
                                                })
                            .value();
    check_reference(fixture, method, psf);
    REQUIRE(assertion_counter().load() > assertions_before);
}
} // namespace docenhance::tests
