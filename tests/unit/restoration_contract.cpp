// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/opencv/restoration.hpp"
#include "linear_fixture.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
namespace docenhance::tests {
TEST_CASE("Wiener admission rejects nonfinite options and invalid PSF identities") {
    for (const auto k : {0.0, 1e-6, 1.01, std::numeric_limits<double>::infinity()}) {
        CHECK(!methods::Wiener::create({.psf = methods::GaussianPsf{}, .k = k}));
    }
    for (const auto blend : {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN()}) {
        CHECK(!methods::Wiener::create({.psf = methods::GaussianPsf{}, .blend = blend}));
    }
    CHECK(!methods::Wiener::create({.psf = methods::GaussianPsf{.sigma = 0.29}}));
    CHECK(!methods::Wiener::create({.psf = methods::MotionPsf{.length = 31.01}}));
    CHECK(!methods::Wiener::create({.psf = methods::MotionPsf{.angle = 180.01}}));
    CHECK(!methods::Wiener::create({.psf = methods::FilePsf{.path = ""}}));
    CHECK(!methods::Wiener::create({.psf = methods::FilePsf{.path = "\xff.png"}}));
    CHECK(methods::Wiener::create({.psf = methods::FilePsf{.path = "PSF é.png"}}));
}
TEST_CASE("PSF normalization rejects invalid raw coefficients before writing") {
    std::array<double, 9> coefficients{};
    CHECK(!methods::normalize_psf(3, 3, coefficients));
    coefficients.front() = 1;
    coefficients.back() = -1;
    const auto before = coefficients;
    CHECK(!methods::normalize_psf(3, 3, coefficients));
    CHECK(coefficients == before);
    coefficients.back() = std::numeric_limits<double>::quiet_NaN();
    CHECK(!methods::normalize_psf(3, 3, coefficients));
    coefficients.back() = 3;
    REQUIRE(methods::normalize_psf(3, 3, coefficients));
    CHECK(coefficients.front() == 0.25);
    CHECK(coefficients.back() == 0.75);
    const auto centroid = methods::psf_centroid(3, 3, coefficients);
    REQUIRE(centroid);
    CHECK(centroid->front() == 0.5);
    CHECK(centroid->back() == 0.5);
    CHECK(!methods::normalize_psf(2, 3, coefficients));
}
TEST_CASE("Restoration guards and charges are checked without native work") {
    CHECK(methods::restoration_fft_size(67).value() == 72);
    CHECK(methods::restoration_fft_size(121).value() == 125);
    CHECK(!methods::restoration_fft_size(0));
    CHECK(!methods::restoration_fft_size(std::numeric_limits<std::uint64_t>::max()));
    const auto charge =
        methods::restoration_charge({.width = 3, .height = 2}, {.width = 72, .height = 72});
    REQUIRE(charge);
    CHECK(charge->candidate == 128);
    CHECK(charge->transfer == std::size_t{4096} * 3 * sizeof(double));
    CHECK(charge->real_plane == std::uint64_t{320} * 72);
    CHECK(charge->complex_plane == std::uint64_t{576} * 72);
    CHECK(charge->native == (std::uint64_t{40} * 72 * 72) + (std::size_t{8} * 1024 * 1024));
}
TEST_CASE("Restoration resource refusal precedes source reads and releases all leases") {
    core::Budget source_budget{4096};
    LinearFixture source{source_budget, {.width = 3, .height = 2}};
    source.fill(0.4);
    core::Budget tiny{std::size_t{8} * 1024 * 1024};
    const auto method = methods::Wiener::create().value();
    auto psf = opencv::resolve_psf(method).value();
    methods::RestorationReport report;
    auto result = opencv::RestorationModel::prepare(source, {}, method, std::move(psf),
                                                    {tiny, {}, report, image::RowUse::measurement});
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::resource);
    CHECK(report.context_samples == 0);
    CHECK(report.native_calls == 0);
    CHECK(tiny.used() == 0);
}
TEST_CASE("Zero blend validates PSF without requiring an FFT budget") {
    core::Budget source_budget{4096};
    LinearFixture source{source_budget, {.width = 3, .height = 2}};
    source.fill(0.4);
    core::Budget tiny{0};
    const auto method = methods::Wiener::create({.blend = 0}).value();
    auto psf = opencv::resolve_psf(method).value();
    methods::RestorationReport report;
    auto result = opencv::RestorationModel::prepare(source, {}, method, psf,
                                                    {tiny, {}, report, image::RowUse::measurement});
    REQUIRE(result);
    CHECK(!result->active());
    CHECK(report.complete);
    CHECK(methods::valid_restoration(report, method));
    psf.coefficients.front() = -1;
    CHECK(!opencv::RestorationModel::prepare(source, {}, method, psf,
                                             {tiny, {}, report, image::RowUse::measurement}));
}
TEST_CASE("Cancellation is observed on both sides of each indivisible native DFT") {
    for (std::size_t allowed = 0; allowed < 6; ++allowed) {
        core::Budget budget{std::size_t{16} * 1024 * 1024};
        LinearFixture source{budget, {.width = 3, .height = 2}};
        source.fill(0.4);
        const auto baseline = budget.used();
        const auto method = methods::Wiener::create().value();
        methods::RestorationReport report;
        const CheckpointStop stop{core::Checkpoint::solving, allowed};
        auto result = opencv::RestorationModel::prepare(
            source, {}, method, opencv::resolve_psf(method).value(),
            {budget, stop.cancellation(), report, image::RowUse::measurement});
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::cancelled);
        CHECK(report.native_calls == (allowed + 1) / 2);
        CHECK(!report.complete);
        CHECK(budget.used() == baseline);
    }
}
TEST_CASE("Restoration observations reject changed pixels without a correction") {
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    LinearFixture source{budget, {.width = 3, .height = 2}};
    source.fill(0.4);
    const auto method = methods::Wiener::create().value();
    methods::RestorationReport report;
    auto model =
        opencv::RestorationModel::prepare(source, {}, method, opencv::resolve_psf(method).value(),
                                          {budget, {}, report, image::RowUse::measurement});
    REQUIRE(model);
    REQUIRE(methods::valid_restoration_observations(report));
    report.evaluated_samples = 1;
    report.changed_samples = 1;
    CHECK(!methods::valid_restoration_observations(report));
}
} // namespace docenhance::tests
