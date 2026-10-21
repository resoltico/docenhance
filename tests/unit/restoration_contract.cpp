// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fft/fft.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/restoration_working.hpp"
#include "gamma_fixture.hpp"
#include "psf_reference.hpp"
#include "restoration_working_detail.hpp"
#include "restoration_working_fixture.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>
namespace docenhance::tests {
TEST_CASE("Analytic PSF factories retain every independent Gaussian and oblique motion tap") {
    constexpr double tolerance = 2e-14;
    for (const auto sigma : {.3, .7, 1.2, 5.}) {
        const auto expected = gaussian_psf_reference(sigma);
        const auto side = methods::psf_side(methods::GaussianPsf{.sigma = sigma});
        REQUIRE(side);
        REQUIRE(std::size_t{*side} * *side == expected.size());
        std::vector<double> actual(expected.size());
        REQUIRE(methods::gaussian_psf({.sigma = sigma}, actual));
        for (std::size_t i = 0; i < actual.size(); ++i) {
            CHECK(std::abs(actual.at(i) - expected.at(i)) < tolerance);
        }
    }
    for (const auto angle : {0., 37., 53., 90., 127., 180.}) {
        constexpr double length = 3.5;
        const auto expected = motion_psf_reference(length, angle);
        const auto side = methods::psf_side(methods::MotionPsf{.length = length, .angle = angle});
        REQUIRE(side);
        REQUIRE(std::size_t{*side} * *side == expected.size());
        std::vector<double> actual(expected.size());
        REQUIRE(methods::motion_psf({.length = length, .angle = angle}, actual));
        for (std::size_t i = 0; i < actual.size(); ++i) {
            CHECK(std::abs(actual.at(i) - expected.at(i)) < tolerance);
        }
    }
    const auto oblique = motion_psf_reference(3.5, 37);
    const auto transposed = motion_psf_reference(3.5, 53);
    bool discriminates = false;
    for (std::size_t i = 0; i < oblique.size(); ++i) {
        discriminates = discriminates || std::abs(oblique.at(i) - transposed.at(i)) > 1e-6;
    }
    CHECK(discriminates);
}
TEST_CASE("The path-free PSF alternatives preserve nonthrowing owned moves") {
    static_assert(std::is_nothrow_move_constructible_v<methods::GaussianPsf>);
    static_assert(std::is_nothrow_move_constructible_v<methods::MotionPsf>);
    static_assert(std::is_nothrow_move_constructible_v<methods::Kernel>);
    static_assert(std::is_nothrow_move_constructible_v<methods::Psf>);
    static_assert(std::is_nothrow_move_assignable_v<methods::Psf>);
    static_assert(std::variant_size_v<methods::Psf> == 3);
    const methods::GaussianPsf gaussian{};
    const methods::MotionPsf motion{};
    const methods::Kernel kernel{};
    for (const methods::Psf& selection : std::array<methods::Psf, 3>{gaussian, motion, kernel}) {
        const methods::WienerParameters parameters{.psf = selection};
        CHECK(!parameters.psf.valueless_by_exception());
        CHECK(methods::valid_parameters(parameters));
        const auto method = methods::Wiener::create(parameters);
        REQUIRE(method);
        CHECK(!method->parameters().psf.valueless_by_exception());
    }
}
TEST_CASE("Wiener admission rejects nonfinite options and invalid kernel identities") {
    for (const auto k : {0.0, 1e-6, 1.01, std::numeric_limits<double>::infinity()}) {
        CHECK(!methods::Wiener::create({.psf = methods::GaussianPsf{}, .k = k}));
    }
    for (const auto blend : {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN()}) {
        CHECK(!methods::Wiener::create({.psf = methods::GaussianPsf{}, .blend = blend}));
    }
    CHECK(!methods::Wiener::create({.psf = methods::GaussianPsf{.sigma = .29}}));
    CHECK(!methods::Wiener::create({.psf = methods::MotionPsf{.length = 31.01}}));
    CHECK(!methods::Wiener::create({.psf = methods::MotionPsf{.angle = 180.01}}));
    const auto kernel = methods::Wiener::create({.psf = methods::Kernel{}});
    REQUIRE(kernel);
    methods::ResolvedPsf resolved{
        .width = 3,
        .height = 3,
        .coefficients = {0, 0, 0, 0, 1, 0, 0, 0, 0},
    };
    REQUIRE(methods::validate_psf(resolved));
    CHECK(!methods::canonical_psf(kernel->parameters(), resolved));
    resolved.source_identity = core::ContentIdentity{.sha256 = std::string(64, 'a'), .bytes = 71};
    CHECK(methods::canonical_psf(kernel->parameters(), resolved));
    resolved.source_identity->bytes = 0;
    CHECK(!methods::canonical_psf(kernel->parameters(), resolved));
    resolved.source_identity->bytes = core::psf_encoded_bytes_max + 1;
    CHECK(!methods::canonical_psf(kernel->parameters(), resolved));
    resolved.source_identity->bytes = 71;
    resolved.source_identity->sha256.front() = 'g';
    CHECK(!methods::canonical_psf(kernel->parameters(), resolved));
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
    CHECK(coefficients.front() == .25);
    CHECK(coefficients.back() == .75);
    const auto centroid = methods::psf_centroid(3, 3, coefficients);
    REQUIRE(centroid);
    CHECK(centroid->front() == .5);
    CHECK(centroid->back() == .5);
    CHECK(!methods::normalize_psf(2, 3, coefficients));
}
TEST_CASE("Restoration geometry and binary64 charge owners are checked without native work") {
    const auto rounded67 = methods::restoration_fft_size(67);
    const auto rounded121 = methods::restoration_fft_size(121);
    REQUIRE(rounded67);
    REQUIRE(rounded121);
    CHECK(*rounded67 == 72);
    CHECK(*rounded121 == 125);
    CHECK(!methods::restoration_fft_size(0));
    CHECK(!methods::restoration_fft_size(std::numeric_limits<std::uint64_t>::max()));
    GammaFixture source{3, 2};
    core::Budget budget{0};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    const auto geometry = methods::restoration_geometry(
        {source.luminance.view(), {}, budget, scheduler}, restoration_gaussian());
    REQUIRE(geometry);
    CHECK(geometry->guard == 32);
    CHECK(geometry->width == 72);
    CHECK(geometry->height == 72);
    const auto real_shape = image::plane_shape(72, 72, sizeof(double));
    const auto complex_shape = image::plane_shape(37, 72, sizeof(fft::Complex));
    REQUIRE(real_shape);
    REQUIRE(complex_shape);
    const auto real_bytes = image::plane_bytes(*real_shape);
    const auto complex_bytes = image::plane_bytes(*complex_shape);
    REQUIRE(real_bytes);
    REQUIRE(complex_bytes);
    CHECK(*real_bytes == std::size_t{576} * 72);
    CHECK(*complex_bytes == std::size_t{640} * 72);
    const auto workspace = fft::workspace_bytes({.width = 72, .height = 72}, 1);
    REQUIRE(workspace);
    CHECK(*workspace > 0);
    CHECK(!fft::workspace_bytes({.width = 0, .height = 72}, 1));
    CHECK(!fft::workspace_bytes({.width = 72, .height = 72}, 0));
    CHECK(budget.used() == 0);
}
TEST_CASE("Restoration cold resource refusal preserves entering storage and refunds owners") {
    GammaFixture source{3, 2};
    const auto before = gamma_bits(source.luminance.view().storage());
    core::Budget cold{0};
    const auto method = methods::Wiener::create();
    REQUIRE(method);
    const auto psf = restoration_gaussian();
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    const auto result =
        methods::apply(*method, psf, {source.luminance.view(), {}, cold, scheduler});
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::resource);
    CHECK(cold.used() == 0);
    CHECK(gamma_bits(source.luminance.view().storage()) == before);
}
TEST_CASE("Zero blend validates PSF without requiring an FFT budget") {
    GammaFixture source{3, 2};
    const auto before = gamma_bits(source.luminance.view().storage());
    core::Budget cold{0};
    const auto method = methods::Wiener::create({.blend = 0});
    REQUIRE(method);
    auto psf = restoration_gaussian();
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    const auto facts = methods::apply(*method, psf, {source.luminance.view(), {}, cold, scheduler});
    REQUIRE(facts);
    CHECK(facts->reason == methods::WienerReason::zero_blend);
    CHECK(facts->counts.evaluated == 0);
    CHECK(!facts->transform);
    CHECK(methods::validate(*facts, method->parameters(), {.width = 3, .height = 2}));
    CHECK(cold.used() == 0);
    CHECK(gamma_bits(source.luminance.view().storage()) == before);
    psf.coefficients.front() = -1;
    const auto invalid =
        methods::apply(*method, psf, {source.luminance.view(), {}, cold, scheduler});
    REQUIRE(!invalid);
    CHECK(invalid.error().code == core::ErrorCode::argument);
    CHECK(cold.used() == 0);
    CHECK(gamma_bits(source.luminance.view().storage()) == before);
}
TEST_CASE("Cancellation is observed around every actual indivisible native Fourier task") {
    GammaFixture source{3, 2};
    const auto method = methods::Wiener::create();
    REQUIRE(method);
    const auto psf = restoration_gaussian();
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    const CheckpointStop observe{core::Checkpoint::solving,
                                 std::numeric_limits<std::size_t>::max()};
    const exec::Scheduler scheduler{*concurrency, observe.cancellation()};
    const auto healthy =
        methods::apply(*method, psf, {source.luminance.view(), {}, budget, scheduler});
    REQUIRE(healthy);
    REQUIRE(healthy->transform);
    const auto visits = CheckpointStop::visits();
    REQUIRE(visits != 0);
    CHECK(visits % 2 == 0);
    CHECK(budget.used() == 0);
    for (std::size_t allowed = 0; allowed < visits; ++allowed) {
        const auto snapshot = gamma_bits(source.luminance.view().storage());
        const CheckpointStop stop{core::Checkpoint::solving, allowed};
        const exec::Scheduler stopped{*concurrency, stop.cancellation()};
        const auto result =
            methods::apply(*method, psf, {source.luminance.view(), {}, budget, stopped});
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::cancelled);
        CHECK(CheckpointStop::visits() == allowed + 1);
        CHECK(budget.used() == 0);
        CHECK(gamma_bits(source.luminance.view().storage()) == snapshot);
    }
}
TEST_CASE("Restoration Facts reject changed pixels without a correction") {
    GammaFixture source{3, 2};
    for (std::uint32_t y = 0; y < 2; ++y) {
        std::ranges::fill(source.luminance.view().row(y), .4);
    }
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    const auto method = methods::Wiener::create();
    REQUIRE(method);
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    const auto facts = methods::apply(*method, restoration_gaussian(),
                                      {source.luminance.view(), {}, budget, scheduler});
    REQUIRE(facts);
    CHECK(methods::validate(*facts));
    auto invalid = *facts;
    invalid.counts.changed = 1;
    invalid.corrected_samples = 0;
    REQUIRE(methods::validate(invalid.counts));
    CHECK(!methods::validate(invalid));
    CHECK(budget.used() == 0);
}
} // namespace docenhance::tests
