// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/report_values.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <utility>

namespace docenhance::methods {
enum class ClassificationReason : std::uint8_t { none };
inline constexpr auto classification_disposition = Disposition::applied;
inline constexpr auto classification_reason = ClassificationReason::none;
struct ClassificationFacts {
    Disposition disposition = classification_disposition;
    ClassificationReason reason = classification_reason;
    std::uint64_t black_samples{};
    std::uint64_t white_samples{};
    bool operator==(const ClassificationFacts&) const = default;
};
[[nodiscard]] constexpr bool validate(const ClassificationFacts& facts) noexcept {
    return facts.disposition == classification_disposition &&
           facts.reason == classification_reason &&
           facts.black_samples <= image::source_pixels_max &&
           facts.white_samples <= image::source_pixels_max - facts.black_samples &&
           facts.black_samples + facts.white_samples != 0;
}
struct OtsuFacts : ClassificationFacts {
    std::uint16_t threshold_bin = otsu_fallback_threshold;
    bool single_bin_fallback = false;
    bool operator==(const OtsuFacts&) const = default;
};
struct SauvolaFacts : ClassificationFacts {
    bool operator==(const SauvolaFacts&) const = default;
};
struct FixedFacts : ClassificationFacts {
    bool operator==(const FixedFacts&) const = default;
};
[[nodiscard]] inline bool validate(const OtsuFacts& facts) noexcept {
    if (!validate(static_cast<const ClassificationFacts&>(facts)) ||
        facts.threshold_bin > otsu_max_threshold ||
        (facts.single_bin_fallback && facts.threshold_bin != otsu_fallback_threshold)) {
        return false;
    }
    return facts.single_bin_fallback ? facts.black_samples == 0 || facts.white_samples == 0
                                     : facts.black_samples != 0 && facts.white_samples != 0;
}
[[nodiscard]] constexpr bool validate(const SauvolaFacts& facts) noexcept {
    return validate(static_cast<const ClassificationFacts&>(facts));
}
[[nodiscard]] constexpr bool validate(const FixedFacts& facts) noexcept {
    return validate(static_cast<const ClassificationFacts&>(facts));
}
[[nodiscard]] inline bool validate(const FixedFacts& facts,
                                   FixedThresholdParameters parameters) noexcept {
    return validate(facts) && valid_parameters(parameters) &&
           (parameters.threshold != 1 || facts.white_samples == 0);
}
[[nodiscard]] inline bool validate(const SauvolaFacts& facts,
                                   SauvolaParameters parameters) noexcept {
    return validate(facts) && valid_parameters(parameters) &&
           (1.0 - parameters.k != 1.0 || facts.black_samples != 0);
}
} // namespace docenhance::methods

namespace docenhance {
template <> struct core::EnumNames<methods::ClassificationReason> {
    static constexpr auto list =
        std::to_array({std::pair{methods::ClassificationReason::none, std::string_view{"none"}}});
};
template <> struct core::Fields<methods::ClassificationFacts> {
    using Value = methods::ClassificationFacts;
    static constexpr auto list = std::tuple{
        core::field("disposition", &Value::disposition,
                    core::Exact{methods::classification_disposition}),
        core::field("reason", &Value::reason, core::Exact{methods::classification_reason}),
        core::field("black_samples", &Value::black_samples, core::Count{}),
        core::field("white_samples", &Value::white_samples, core::Count{}),
    };
};
template <> struct core::WireTag<methods::OtsuFacts> {
    static constexpr std::string_view key = "method";
    static constexpr auto value = methods::Otsu::descriptor().id;
};
template <> struct core::WireTag<methods::SauvolaFacts> {
    static constexpr std::string_view key = "method";
    static constexpr auto value = methods::Sauvola::descriptor().id;
};
template <> struct core::WireTag<methods::FixedFacts> {
    static constexpr std::string_view key = "method";
    static constexpr auto value = methods::FixedThreshold::descriptor().id;
};
template <> struct core::Fields<methods::OtsuFacts> {
    static constexpr auto list = std::tuple_cat(
        core::Fields<methods::ClassificationFacts>::list,
        std::tuple{
            core::field(
                "threshold_bin", &methods::OtsuFacts::threshold_bin,
                core::Range<std::uint16_t>{.minimum = 0, .maximum = methods::otsu_max_threshold}),
            core::field("single_bin_fallback", &methods::OtsuFacts::single_bin_fallback),
        });
};
template <>
struct core::Fields<methods::SauvolaFacts> : core::Fields<methods::ClassificationFacts> {};
template <>
struct core::Fields<methods::FixedFacts> : core::Fields<methods::ClassificationFacts> {};
} // namespace docenhance
