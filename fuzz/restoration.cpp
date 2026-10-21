// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/restoration.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/restoration_working.hpp"
#include "support/entry_point.hpp"
#include "support/fft_dispatch.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>
namespace {
namespace image = docenhance::image;
namespace methods = docenhance::methods;
using docenhance::fuzz::require;
methods::ResolvedPsf gaussian_psf(methods::GaussianPsf parameters) {
    const auto side = methods::psf_side(parameters).value();
    methods::ResolvedPsf psf{
        .width = side,
        .height = side,
        .coefficients = std::vector<double>(std::size_t{side} * side),
    };
    require(methods::gaussian_psf(parameters, psf.coefficients).has_value(),
            "actual canonical Gaussian producer");
    const auto centroid = methods::psf_centroid(side, side, psf.coefficients).value();
    psf.centroid_x = centroid.front();
    psf.centroid_y = centroid.back();
    return psf;
}
std::complex<double> transfer(const methods::ResolvedPsf& psf) {
    std::complex<double> value{};
    for (std::uint32_t y = 0; y < psf.height; ++y) {
        for (std::uint32_t x = 0; x < psf.width; ++x) {
            const auto displacement =
                static_cast<double>(x) - (static_cast<double>(psf.width - 1) / 2);
            const auto phase = -std::numbers::pi * displacement / 2;
            value += psf.coefficients.at((std::size_t{y} * psf.width) + x) * std::polar(1.0, phase);
        }
    }
    return value;
}
void check_phase_output(image::PlaneView<const double> plane, const methods::ResolvedPsf& psf,
                        const methods::Wiener& method) {
    const auto h = transfer(psf);
    const auto filter = std::conj(h) / (std::norm(h) + method.parameters().k);
    const auto blend = method.parameters().blend;
    for (std::uint32_t y = 0; y < plane.height(); ++y) {
        const auto row = plane.row(y);
        for (std::size_t x = 0; x < row.size(); ++x) {
            const auto wave = std::polar(1.0, std::numbers::pi * static_cast<double>(x) / 2);
            const auto original = 0.4 + (0.1 * std::real(wave));
            const auto candidate = 0.4 + (0.1 * std::real(filter * wave));
            const auto expected =
                std::clamp(((1 - blend) * original) + (blend * candidate), 0.0, 1.0);
            const auto error = std::abs(row[x] - expected);
            require(std::isfinite(error) && error < 1e-12,
                    "independent kernel transfer reflected crop/DC/blend");
        }
    }
}
void phase_case(double k, double blend, double sigma) {
    docenhance::core::Budget budget{std::size_t{16} * 1024 * 1024};
    const docenhance::exec::Scheduler scheduler{
        docenhance::exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto method = methods::Wiener::create(
                            {.psf = methods::GaussianPsf{.sigma = sigma}, .k = k, .blend = blend})
                            .value();
    const auto psf = gaussian_psf({.sigma = sigma});
    {
        auto plane = image::Plane<double>::allocate(budget, 3, 3).value();
        auto replay = image::Plane<double>::allocate(budget, 3, 3).value();
        std::ranges::fill(plane.view().storage(), -1);
        for (std::uint32_t y = 0; y < 3; ++y) {
            for (std::uint32_t x = 0; x < 3; ++x) {
                plane.view().row(y)[x] =
                    0.4 + (0.1 * std::cos((std::numbers::pi * static_cast<double>(x)) / 2));
            }
        }
        std::ranges::copy(plane.view().storage(), replay.view().storage().begin());
        const auto held = budget.used();
        const auto result = methods::apply(method, psf, {plane.view(), {}, budget, scheduler});
        const auto repeated = methods::apply(method, psf, {replay.view(), {}, budget, scheduler});
        require(result && repeated && *result == *repeated &&
                    methods::validate(*result, method.parameters(), {.width = 3, .height = 3}),
                "actual Working restoration repeats complete typed facts from identical input");
        require(result->transform && result->transform->guard == 32 &&
                    result->transform->width == 72 && result->transform->height == 72 &&
                    std::abs(result->transform->padded_mean - 0.4) < 1e-15,
                "actual reflected periodic Gaussian geometry/DC mean");
        check_phase_output(plane.view().as_const(), psf, method);
        const auto output_bytes = std::as_bytes(plane.view().storage());
        const auto replay_bytes = std::as_bytes(replay.view().storage());
        require(std::ranges::equal(output_bytes, replay_bytes) && budget.used() == held,
                "actual repeat preserves output/padding bits and refunds all scratch");
    }
    require(budget.used() == 0, "actual restoration payload refunded");
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    docenhance::fuzz::FuzzInput input{{data, size}};
    const auto k = 1e-5 + ((static_cast<double>(input.byte()) / 255) * (1 - 1e-5));
    const auto blend = 0.01 + ((static_cast<double>(input.byte()) / 255) * 0.99);
    const auto selection = input.byte();
    const auto sigma = 0.3 + ((static_cast<double>(selection) / 255) * 1.2);
    docenhance::fuzz::check_fft_dispatch(selection % docenhance::fuzz::dispatch_samples);
    phase_case(k, blend, sigma);
    return 0;
}
