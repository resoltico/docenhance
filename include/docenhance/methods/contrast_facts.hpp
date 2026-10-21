// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/report_values.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace docenhance::methods {
enum class LevelsReason : std::uint8_t {
    none,
    zero_blend,
    no_eligible_samples,
    insufficient_dynamic_range,
    no_effect,
};
enum class GammaReason : std::uint8_t {
    none,
    zero_blend,
    no_eligible_samples,
    identity_gamma,
    no_effect,
};
enum class ClaheReason : std::uint8_t { none, zero_blend, no_eligible_samples, no_effect };
struct LevelsFacts {
    Disposition disposition = Disposition::no_change;
    LevelsReason reason = LevelsReason::no_eligible_samples;
    PixelCounts counts{};
    std::optional<LevelsRange> levels = std::nullopt;
    std::uint64_t clipped_low_samples{};
    std::uint64_t clipped_high_samples{};
    bool operator==(const LevelsFacts&) const = default;
};
struct GammaFacts {
    Disposition disposition = Disposition::no_change;
    GammaReason reason = GammaReason::no_eligible_samples;
    PixelCounts counts{};
    bool operator==(const GammaFacts&) const = default;
};
struct ClaheFacts {
    Disposition disposition = Disposition::no_change;
    ClaheReason reason = ClaheReason::no_eligible_samples;
    PixelCounts counts{};
    std::uint32_t identity_tiles{};
    bool operator==(const ClaheFacts&) const = default;
};
[[nodiscard]] bool validate(const LevelsFacts& facts) noexcept;
[[nodiscard]] bool validate(const GammaFacts& facts) noexcept;
[[nodiscard]] bool validate(const ClaheFacts& facts) noexcept;
[[nodiscard]] bool validate(const LevelsFacts& facts, LevelsParameters parameters) noexcept;
[[nodiscard]] bool validate(const GammaFacts& facts, GammaParameters parameters) noexcept;
[[nodiscard]] bool validate(const ClaheFacts& facts, ClaheParameters parameters) noexcept;
} // namespace docenhance::methods

namespace docenhance {
template <> struct core::EnumNames<methods::LevelsReason> {
    using Value = methods::LevelsReason;
    static constexpr std::array list{
        std::pair{Value::none, std::string_view{"none"}},
        std::pair{Value::zero_blend, std::string_view{"zero_blend"}},
        std::pair{Value::no_eligible_samples, std::string_view{"no_eligible_samples"}},
        std::pair{
            Value::insufficient_dynamic_range,
            std::string_view{"insufficient_dynamic_range"},
        },
        std::pair{Value::no_effect, std::string_view{"no_effect"}},
    };
};
template <> struct core::EnumNames<methods::GammaReason> {
    using Value = methods::GammaReason;
    static constexpr std::array list{
        std::pair{Value::none, std::string_view{"none"}},
        std::pair{Value::zero_blend, std::string_view{"zero_blend"}},
        std::pair{Value::no_eligible_samples, std::string_view{"no_eligible_samples"}},
        std::pair{Value::identity_gamma, std::string_view{"identity_gamma"}},
        std::pair{Value::no_effect, std::string_view{"no_effect"}},
    };
};
template <> struct core::EnumNames<methods::ClaheReason> {
    using Value = methods::ClaheReason;
    static constexpr std::array list{
        std::pair{Value::none, std::string_view{"none"}},
        std::pair{Value::zero_blend, std::string_view{"zero_blend"}},
        std::pair{Value::no_eligible_samples, std::string_view{"no_eligible_samples"}},
        std::pair{Value::no_effect, std::string_view{"no_effect"}},
    };
};
template <> struct core::WireTag<methods::LevelsFacts> {
    static constexpr std::string_view key = "method";
    static constexpr auto value = methods::Levels::descriptor().id;
};
template <> struct core::WireTag<methods::GammaFacts> {
    static constexpr std::string_view key = "method";
    static constexpr auto value = methods::Gamma::descriptor().id;
};
template <> struct core::WireTag<methods::ClaheFacts> {
    static constexpr std::string_view key = "method";
    static constexpr auto value = methods::Clahe::descriptor().id;
};
template <> struct core::Fields<methods::LevelsFacts> {
    using Value = methods::LevelsFacts;
    static constexpr auto list = std::tuple{
        core::field("disposition", &Value::disposition),
        core::field("reason", &Value::reason),
        core::field("counts", &Value::counts),
        core::field("levels", &Value::levels),
        core::field("clipped_low_samples", &Value::clipped_low_samples, core::Count{}),
        core::field("clipped_high_samples", &Value::clipped_high_samples, core::Count{}),
    };
};
template <> struct core::Fields<methods::GammaFacts> {
    using Value = methods::GammaFacts;
    static constexpr auto list = std::tuple{
        core::field("disposition", &Value::disposition),
        core::field("reason", &Value::reason),
        core::field("counts", &Value::counts),
    };
};
template <> struct core::Fields<methods::ClaheFacts> {
    using Value = methods::ClaheFacts;
    static constexpr auto list = std::tuple{
        core::field("disposition", &Value::disposition),
        core::field("reason", &Value::reason),
        core::field("counts", &Value::counts),
        core::field("identity_tiles", &Value::identity_tiles,
                    core::Range<std::uint32_t>{
                        .minimum = 0,
                        .maximum = methods::clahe_tiles_max,
                    }),
    };
};
} // namespace docenhance
