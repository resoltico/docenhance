// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/image/gaussian.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameter_domains.hpp"
#include "docenhance/methods/report_values.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace docenhance::methods {
enum class MorphologyReason : std::uint8_t {
    none,
    zero_strength,
    unit_gain,
    no_eligible_samples,
    no_effect,
};
struct MorphologyAnalysis {
    std::uint32_t stride{};
    std::uint32_t count{};
    bool fallback = false;
    double analysis_fill{};
    double target{};
    double background_q10{};
    double background_q50{};
    double background_q90{};
    double background_min{};
    double background_max{};
    bool operator==(const MorphologyAnalysis&) const = default;
};
struct MorphologyFacts {
    Disposition disposition = Disposition::no_change;
    MorphologyReason reason = MorphologyReason::no_eligible_samples;
    PixelCounts counts{};
    std::uint32_t resolved_radius = background_radius_domain.minimum;
    double sigma = static_cast<double>(background_radius_domain.minimum) / 2;
    std::uint32_t gaussian_radius = 2;
    std::optional<MorphologyAnalysis> analysis = std::nullopt;
    std::uint64_t gain_capped_samples{};
    std::uint64_t saturated_samples{};
    double min_gain = 1;
    double max_gain = 1;
    bool operator==(const MorphologyFacts&) const = default;
};
[[nodiscard]] bool validate(const MorphologyAnalysis& analysis) noexcept;
[[nodiscard]] bool validate(const MorphologyFacts& facts) noexcept;
[[nodiscard]] bool validate(const MorphologyFacts& facts, MorphologyParameters parameters,
                            image::Extent extent) noexcept;
} // namespace docenhance::methods

namespace docenhance {
template <> struct core::EnumNames<methods::MorphologyReason> {
    using Value = methods::MorphologyReason;
    static constexpr std::array list{
        std::pair{Value::none, std::string_view{"none"}},
        std::pair{Value::zero_strength, std::string_view{"zero_strength"}},
        std::pair{Value::unit_gain, std::string_view{"unit_gain"}},
        std::pair{Value::no_eligible_samples, std::string_view{"no_eligible_samples"}},
        std::pair{Value::no_effect, std::string_view{"no_effect"}},
    };
};
template <> struct core::WireTag<methods::MorphologyFacts> {
    static constexpr std::string_view key = "method";
    static constexpr auto value = methods::Morphology::descriptor().id;
};
template <> struct core::Fields<methods::MorphologyAnalysis> {
    using Value = methods::MorphologyAnalysis;
    static constexpr auto list = std::tuple{
        core::field("stride", &Value::stride, core::Count{}),
        core::field("count", &Value::count, core::Count{}),
        core::field("fallback", &Value::fallback),
        core::field("analysis_fill", &Value::analysis_fill, core::Unit{}),
        core::field("target", &Value::target, core::Unit{}),
        core::field("background_q10", &Value::background_q10, core::Unit{}),
        core::field("background_q50", &Value::background_q50, core::Unit{}),
        core::field("background_q90", &Value::background_q90, core::Unit{}),
        core::field("background_min", &Value::background_min, core::Unit{}),
        core::field("background_max", &Value::background_max, core::Unit{}),
    };
};
template <> struct core::Fields<methods::MorphologyFacts> {
    using Value = methods::MorphologyFacts;
    static constexpr auto list = std::tuple{
        core::field("disposition", &Value::disposition),
        core::field("reason", &Value::reason),
        core::field("counts", &Value::counts),
        core::field("resolved_radius", &Value::resolved_radius, methods::background_radius_domain),
        core::field("sigma", &Value::sigma,
                    core::Range<double>{
                        .minimum = methods::background_radius_domain.minimum / 2.0,
                        .maximum = methods::background_radius_domain.maximum / 2.0,
                    }),
        core::field(
            "gaussian_radius", &Value::gaussian_radius,
            core::Range<std::uint32_t>{.minimum = 1, .maximum = image::gaussian_radius_limit}),
        core::field("analysis", &Value::analysis),
        core::field("gain_capped_samples", &Value::gain_capped_samples, core::Count{}),
        core::field("saturated_samples", &Value::saturated_samples, core::Count{}),
        core::field("min_gain", &Value::min_gain, methods::background_max_gain_domain),
        core::field("max_gain", &Value::max_gain, methods::background_max_gain_domain),
    };
};
} // namespace docenhance
