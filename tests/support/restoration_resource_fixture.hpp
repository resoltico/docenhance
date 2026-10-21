// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fft/fft.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/restoration_working.hpp"
#include "fft_detail.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>
namespace docenhance::tests {
inline constexpr std::size_t restoration_resource_limit = std::size_t{128} * 1024 * 1024;
inline methods::Wiener resource_wiener() {
    auto result = methods::Wiener::create({.psf = methods::GaussianPsf{.sigma = 1.2}});
    require(result.has_value(), "resource method admits original Gaussian sigma1.2");
    return *result;
}
inline methods::ResolvedPsf resource_psf() {
    const methods::GaussianPsf parameters{.sigma = 1.2};
    const auto side = methods::psf_side(parameters);
    require(side.has_value(), "resource PSF has admitted exact support");
    methods::ResolvedPsf result{
        .width = *side,
        .height = *side,
        .coefficients = std::vector<double>(std::size_t{*side} * *side),
    };
    require(methods::gaussian_psf(parameters, result.coefficients).has_value(),
            "actual sole Gaussian producer fills the bounded PSF");
    const auto centroid = methods::psf_centroid(*side, *side, result.coefficients);
    require(centroid.has_value(), "actual Gaussian PSF has a valid centroid");
    result.centroid_x = centroid->front();
    result.centroid_y = centroid->back();
    require(methods::validate_psf(result).has_value(), "resource PSF is actually validated");
    return result;
}
inline exec::Concurrency resource_concurrency() {
    const auto admitted = exec::Concurrency::resolve(1, 1, 0, 0);
    require(admitted.has_value(), "whole-method resource call has one actual scheduler worker");
    return *admitted;
}
struct RestorationResourceFixture {
    RestorationResourceFixture(core::Budget& owner, image::Extent extent) : budget(owner) {
        auto y = image::Plane<double>::allocate(owner, extent.width, extent.height);
        auto mask = image::Plane<std::uint8_t>::allocate(owner, extent.width, extent.height);
        require(y && mask, "resource fixture owns charged real Y and protection storage");
        luminance = std::move(*y);
        protection = std::move(*mask);
        std::ranges::fill(luminance.view().storage(), -1);
        std::ranges::fill(protection.view().storage(), 255);
        for (std::uint32_t row = 0; row < extent.height; ++row) {
            for (std::uint32_t x = 0; x < extent.width; ++x) {
                luminance.view().row(row)[x] =
                    0.2 + (static_cast<double>((x + (17 * row)) % 997) / 1994);
                protection.view().row(row)[x] = 0;
            }
        }
        if (std::uint64_t{extent.width} * extent.height > 1) {
            protection.view().row(0)[0] = 255;
        }
        entering = gamma_bits(luminance.view().storage());
        mask_entering.assign(protection.view().storage().begin(),
                             protection.view().storage().end());
    }
    methods::Working working(const exec::Scheduler& scheduler) {
        return {luminance.view(), protection.view().as_const(), budget.get(), scheduler};
    }
    void preserved(bool all_y) {
        const auto output = luminance.view().storage();
        const auto pitch = luminance.view().row_pitch();
        for (std::size_t i = 0; i < output.size(); ++i) {
            const auto x = i % pitch;
            const auto y = i / pitch;
            const bool untouched = all_y || x >= luminance.width() ||
                                   protection.view().row(static_cast<std::uint32_t>(y))[x] != 0;
            require(
                untouched ? std::bit_cast<std::uint64_t>(output[i]) == entering.at(i)
                          : std::isfinite(output[i]) && output[i] >= 0 && output[i] <= 1,
                "actual full method preserves unstarted/protected/padding bits and finite output");
        }
        require(std::ranges::equal(protection.view().storage(), mask_entering),
                "full-method mask backing is immutable");
    }
    std::reference_wrapper<core::Budget> budget;
    image::Plane<double> luminance;
    image::Plane<std::uint8_t> protection;
    std::vector<std::uint64_t> entering;
    std::vector<std::uint8_t> mask_entering;
};
inline std::size_t resource_plane(std::uint32_t w, std::uint32_t h, std::size_t sample) {
    const auto shape = image::plane_shape(w, h, sample);
    require(shape.has_value(), "resource plane shape is admitted without overflow");
    const auto bytes = image::plane_bytes(*shape);
    require(bytes.has_value(), "resource plane byte extent is admitted without overflow");
    return *bytes;
}
struct RestorationResourceBound {
    image::Extent transform;
    std::size_t real, spectrum, sums, observations, native, payload, peak;
    std::size_t row_capacity, column_capacity;
};
inline RestorationResourceBound resource_bound(image::Extent extent) {
    const auto width = methods::restoration_fft_size(std::uint64_t{extent.width} + 64);
    const auto height = methods::restoration_fft_size(std::uint64_t{extent.height} + 64);
    require(width && height, "original Gaussian1.2 has guard32 and admitted padded smooth extents");
    RestorationResourceBound result{};
    result.transform = {.width = *width, .height = *height};
    result.real = resource_plane(*width, *height, sizeof(double));
    result.spectrum = resource_plane((*width / 2) + 1, *height, sizeof(fft::Complex));
    result.sums = resource_plane(*height, 1, sizeof(double));
    constexpr std::uint32_t count_rows = 7;
    result.observations =
        resource_plane((extent.height + 63) / 64, count_rows, sizeof(std::uint64_t));
    const auto native = fft::workspace_bytes(result.transform, 1);
    require(native.has_value(), "actual FFT owner supplies its finite serial opaque capacity");
    result.native = *native;
    const auto rows = fft::phase_bytes(result.transform, 1, fft::Axis::rows);
    const auto columns = fft::phase_bytes(result.transform, 1, fft::Axis::columns);
    require(rows && columns && std::max(*rows, *columns) == result.native,
            "actual row/column reservations agree with the sole workspace capacity owner");
    result.row_capacity = *rows;
    result.column_capacity = *columns;
    result.payload = std::max({
        result.real + result.sums,
        result.real + (2 * result.spectrum),
        result.real + result.observations,
    });
    result.peak = std::max({
        result.real + result.sums,
        result.real + (2 * result.spectrum) + result.native,
        result.real + result.observations,
    });
    return result;
}
inline void resource_facts(const methods::WienerFacts& facts, image::Extent extent) {
    const auto method = resource_wiener();
    require(methods::validate(facts, method.parameters(), extent) && facts.transform &&
                facts.context_samples == std::uint64_t{extent.width} * extent.height &&
                facts.counts.evaluated == facts.counts.eligible,
            "actual full Wiener traversal returns validated geometry/context/evaluated Facts");
    require(facts.context_samples == 1 || facts.counts.changed > 0,
            "nonconstant original shapes attain actual changed output stores");
}
} // namespace docenhance::tests
