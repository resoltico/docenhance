// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/denoising.hpp"

#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/tvl1.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <variant>
namespace docenhance::methods {
core::Result<Nlm> Nlm::create(NlmParameters p) {
    if (!std::isfinite(p.h) || p.h < nlm_min_h || p.h > nlm_max_h || !std::isfinite(p.blend) ||
        p.blend < 0 || p.blend > 1 || p.patch < nlm_min_patch || p.patch > nlm_max_patch ||
        p.patch % 2 == 0 || p.search < nlm_min_search || p.search > nlm_max_search ||
        p.search % 2 == 0 || p.search < p.patch) {
        return core::failure(core::ErrorCode::argument, "Invalid NLM strength, blend or windows");
    }
    return Nlm{p};
}
double nlm_native_strength(const NlmParameters& p) noexcept {
    constexpr float scale =
        static_cast<float>(image::word_max) / static_cast<float>(image::byte_max);
    return static_cast<double>(scale * static_cast<float>(p.h));
}
std::string_view status_name(DenoiseStatus s) noexcept {
    switch (s) {
    case DenoiseStatus::disabled:
        return "disabled";
    case DenoiseStatus::no_change:
        return "no_change";
    case DenoiseStatus::applied:
        return "applied";
    case DenoiseStatus::failed:
        return "failed";
    }
    return "failed";
}
std::string_view reason_name(DenoiseReason r) noexcept {
    switch (r) {
    case DenoiseReason::none:
        return "none";
    case DenoiseReason::zero_blend:
        return "zero_blend";
    case DenoiseReason::no_eligible_samples:
        return "no_eligible_samples";
    case DenoiseReason::no_effect:
        return "no_effect";
    case DenoiseReason::processing_failure:
        return "processing_failure";
    }
    return "processing_failure";
}
core::Result<std::uint16_t> nlm_quantize(const image::Rgb& rgb) {
    auto y = image::luminance(rgb);
    if (!y) {
        return std::unexpected(y.error());
    }
    auto f = image::srgb_encode(*y);
    if (!f) {
        return std::unexpected(f.error());
    }
    constexpr double maximum = image::word_max;
    constexpr double half_sample = 0.5;
    return static_cast<std::uint16_t>(std::floor((maximum * *f) + half_sample));
}
core::Result<image::Rgb> nlm_correct(const image::Rgb& rgb, std::uint16_t input,
                                     std::uint16_t output, double blend) {
    auto y = image::luminance(rgb);
    if (!y) {
        return std::unexpected(y.error());
    }
    if (!std::isfinite(blend) || blend < 0 || blend > 1) {
        return core::failure(core::ErrorCode::argument, "Invalid denoising blend");
    }
    if (input == output || blend == 0) {
        return rgb;
    }
    auto f = image::srgb_encode(*y);
    if (!f) {
        return std::unexpected(f.error());
    }
    constexpr double maximum = image::word_max;
    const auto delta = static_cast<std::int32_t>(output) - static_cast<std::int32_t>(input);
    const auto candidate = std::clamp(*f + (static_cast<double>(delta) / maximum), 0.0, 1.0);
    return image::blend_perceptual(rgb, *f, candidate, blend);
}
core::Result<std::size_t> nlm_native_scratch(image::Extent e, const Nlm& method) {
    const auto p = method.parameters();
    if (e.width == 0 || e.height == 0 || e.width > nlm_native_extent ||
        e.height > nlm_native_extent) {
        return core::failure(core::ErrorCode::argument, "Native NLM tile exceeds its extent bound");
    }
    const std::size_t r = (p.patch + p.search - 2) / 2;
    constexpr std::size_t controls = 65536;
    constexpr std::size_t weights = 65536 * sizeof(int);
    return (sizeof(std::uint16_t) * (e.width + (2 * r)) * (e.height + (2 * r))) +
           (sizeof(int) * p.search * p.search * (1 + p.patch + e.width)) + weights + controls;
}
namespace {
bool valid_native_observations(const DenoisingReport& r) noexcept {
    if (r.preparation_charge_peak > core::continuous_processing_budget) {
        return false;
    }
    if (r.native_calls == 0) {
        return r.native_reserved_peak == 0 && r.preparation_charge_peak == 0;
    }
    return r.native_reserved_peak > 0 && r.preparation_charge_peak >= r.native_reserved_peak;
}
} // namespace
bool valid_denoising(const DenoisingReport& r, const Denoising& requested) {
    if (const auto* const tv = std::get_if<Tvl1>(&requested)) {
        return r.changed_samples <= r.corrected_samples &&
               r.corrected_samples <= r.evaluated_samples &&
               r.evaluated_samples <= r.eligible_samples && valid_tvl1_report(r, *tv);
    }
    if (r.tvl1) {
        return false;
    }
    const auto* const method = std::get_if<Nlm>(&requested);
    if (!r.complete || r.status == DenoiseStatus::failed ||
        r.changed_samples > r.corrected_samples || r.corrected_samples > r.evaluated_samples ||
        r.evaluated_samples > r.eligible_samples) {
        return false;
    }
    if (method == nullptr) {
        return !r.requested && r.status == DenoiseStatus::disabled &&
               r.reason == DenoiseReason::none && r.native_h == 0 && r.native_calls == 0 &&
               r.evaluated_samples == 0 && r.corrected_samples == 0 && r.changed_samples == 0 &&
               r.native_reserved_peak == 0 && r.preparation_charge_peak == 0;
    }
    const auto* const parameters =
        r.requested ? std::get_if<NlmParameters>(&*r.requested) : nullptr;
    if (parameters == nullptr || *parameters != method->parameters() ||
        r.native_h != nlm_native_strength(*parameters)) {
        return false;
    }
    if (!valid_native_observations(r)) {
        return false;
    }
    if (r.status == DenoiseStatus::applied) {
        return r.reason == DenoiseReason::none && r.changed_samples > 0 &&
               r.evaluated_samples == r.eligible_samples && r.native_calls > 0;
    }
    if (r.status != DenoiseStatus::no_change || r.changed_samples != 0) {
        return false;
    }
    if (r.reason == DenoiseReason::zero_blend) {
        return parameters->blend == 0 && r.native_calls == 0 && r.evaluated_samples == 0;
    }
    if (r.reason == DenoiseReason::no_eligible_samples) {
        return r.eligible_samples == 0 && r.native_calls == 0 && r.evaluated_samples == 0;
    }
    return r.reason == DenoiseReason::no_effect && r.evaluated_samples == r.eligible_samples &&
           r.native_calls > 0;
}
bool valid_denoising_extent(const DenoisingReport& report, image::Extent extent) {
    const auto& r = report;
    if (r.requested && std::holds_alternative<Tvl1Parameters>(*r.requested)) {
        if (!r.tvl1) {
            return false;
        }
        const auto& detail = *r.tvl1;
        if (detail.field_bytes == 0) {
            return true;
        }
        auto shape = image::plane_shape(extent.width, extent.height, sizeof(double));
        auto bytes = shape ? image::plane_bytes(*shape)
                           : core::Result<std::size_t>{
                                 core::failure(core::ErrorCode::argument, "Invalid TV-L1 extent")};
        return bytes && detail.field_bytes == *bytes;
    }
    if (r.native_calls == 0) {
        return true;
    }
    if (!r.requested) {
        return false;
    }
    const auto* const parameters = std::get_if<NlmParameters>(&*r.requested);
    if (parameters == nullptr) {
        return false;
    }
    auto method = Nlm::create(*parameters);
    if (!method || parameters->search > std::min(extent.width, extent.height)) {
        return false;
    }
    const auto p = method->parameters();
    const auto radius = (p.search + p.patch - 2) / 2;
    auto scratch = nlm_native_scratch(
        {
            .width = std::min(nlm_tile_width, extent.width) + (2 * radius),
            .height = std::min(nlm_tile_width, extent.height) + (2 * radius),
        },
        *method);
    const auto expected = ((std::uint64_t{extent.width} + nlm_tile_width - 1) / nlm_tile_width) *
                          ((std::uint64_t{extent.height} + nlm_tile_width - 1) / nlm_tile_width);
    return scratch && r.native_reserved_peak == *scratch && r.native_calls == expected;
}
} // namespace docenhance::methods
