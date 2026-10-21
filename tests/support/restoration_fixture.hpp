// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <variant>

namespace docenhance::tests {
inline constexpr image::Extent restoration_fact_extent{.width = 16, .height = 8};
inline methods::PsfFacts
analytic_psf_facts(const methods::Psf& parameters = methods::GaussianPsf{}) {
    constexpr std::size_t maximum_coefficients = std::size_t{35} * 35;
    std::array<double, maximum_coefficients> coefficients{};
    const auto side = methods::psf_side(parameters).value();
    const auto values = std::span{coefficients}.first(std::size_t{side} * side);
    methods::PsfKind kind = methods::PsfKind::gaussian;
    if (const auto* const gaussian = std::get_if<methods::GaussianPsf>(&parameters)) {
        methods::gaussian_psf(*gaussian, values).value();
    } else if (const auto* const motion = std::get_if<methods::MotionPsf>(&parameters)) {
        methods::motion_psf(*motion, values).value();
        kind = methods::PsfKind::motion;
    } else {
        throw std::runtime_error("The fixture requires an analytic PSF");
    }
    const auto centroid = methods::psf_centroid(side, side, values).value();
    double sum = 0;
    for (const auto value : values) {
        sum += value;
    }
    return {
        .kind = kind,
        .width = side,
        .height = side,
        .normalized_sum = sum,
        .centroid_x = centroid.front(),
        .centroid_y = centroid.back(),
    };
}
inline methods::WienerFacts completed_wiener_facts() {
    return {
        .disposition = methods::Disposition::applied,
        .reason = methods::WienerReason::none,
        .counts = {.eligible = 128, .evaluated = 128, .changed = 60},
        .psf = analytic_psf_facts(),
        .context_samples = 128,
        .corrected_samples = 65,
        .transform =
            methods::RestorationTransform{
                .guard = 32,
                .width = 80,
                .height = 72,
                .padded_mean = 0.4,
            },
        .raw_low_samples = 5,
        .raw_high_samples = 7,
        .blended_low_samples = 2,
        .blended_high_samples = 3,
    };
}
inline methods::WienerFacts tiny_blend_wiener_facts() {
    const methods::WienerParameters parameters{.blend = std::numeric_limits<double>::denorm_min()};
    const auto method = methods::Wiener::create(parameters).value();
    const auto blended = methods::restoration_target(1, -1, method);
    auto facts = completed_wiener_facts();
    facts.disposition = methods::Disposition::no_change;
    facts.reason = methods::WienerReason::no_effect;
    facts.counts.changed = static_cast<std::uint64_t>(std::clamp(blended, 0.0, 1.0) != 1);
    facts.corrected_samples = static_cast<std::uint64_t>(blended != 1);
    facts.raw_low_samples = 1;
    facts.raw_high_samples = 0;
    facts.blended_low_samples = static_cast<std::uint64_t>(blended < 0);
    facts.blended_high_samples = static_cast<std::uint64_t>(blended > 1);
    return facts;
}
inline methods::PsfFacts edge_psf_facts(std::uint32_t width, std::uint32_t height, bool vertical,
                                        bool positive) {
    constexpr auto maximum_coefficients =
        std::size_t{methods::psf_maximum_side} * methods::psf_maximum_side;
    constexpr std::uint32_t weight_period = 3;
    std::array<double, maximum_coefficients> coefficients{};
    const auto values = std::span{coefficients}.first(std::size_t{width} * height);
    const auto side = vertical ? width : height;
    const auto edge = positive ? side - 1 : 0;
    const auto count = vertical ? height : width;
    for (std::uint32_t sample = 0; sample < count; ++sample) {
        const auto index =
            vertical ? (std::size_t{sample} * width) + edge : (std::size_t{edge} * width) + sample;
        values.subspan(index, 1).front() = 1 + (sample % weight_period);
    }
    methods::normalize_psf(width, height, values).value();
    const auto centroid = methods::psf_centroid(width, height, values).value();
    double sum = 0;
    for (const auto value : values) {
        sum += value;
    }
    return {
        .kind = methods::PsfKind::kernel,
        .width = width,
        .height = height,
        .normalized_sum = sum,
        .centroid_x = centroid.front(),
        .centroid_y = centroid.back(),
    };
}
} // namespace docenhance::tests
