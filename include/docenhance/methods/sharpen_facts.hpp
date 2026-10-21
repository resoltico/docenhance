// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace docenhance::methods {
enum class UnsharpReason : std::uint8_t { none, zero_amount, no_eligible_samples, no_effect };
struct UnsharpFacts {
    Disposition disposition = Disposition::no_change;
    UnsharpReason reason = UnsharpReason::no_eligible_samples;
    PixelCounts counts{};
    std::uint64_t context_samples{};
    std::uint64_t corrected_samples{};
    std::optional<ExcursionRange> pre_clamp = std::nullopt;
    std::uint64_t clipped_low_samples{};
    std::uint64_t clipped_high_samples{};
    bool operator==(const UnsharpFacts&) const = default;
};
[[nodiscard]] bool validate(const UnsharpFacts& facts) noexcept;
[[nodiscard]] bool validate(const UnsharpFacts& facts, UnsharpParameters parameters) noexcept;
} // namespace docenhance::methods

namespace docenhance {
template <> struct core::EnumNames<methods::UnsharpReason> {
    using Value = methods::UnsharpReason;
    static constexpr std::array list{
        std::pair{Value::none, std::string_view{"none"}},
        std::pair{Value::zero_amount, std::string_view{"zero_amount"}},
        std::pair{Value::no_eligible_samples, std::string_view{"no_eligible_samples"}},
        std::pair{Value::no_effect, std::string_view{"no_effect"}},
    };
};
template <> struct core::WireTag<methods::UnsharpFacts> {
    static constexpr std::string_view key = "method";
    static constexpr auto value = methods::Unsharp::descriptor().id;
};
template <> struct core::Fields<methods::UnsharpFacts> {
    using Value = methods::UnsharpFacts;
    static constexpr auto list = std::tuple{
        core::field("disposition", &Value::disposition),
        core::field("reason", &Value::reason),
        core::field("counts", &Value::counts),
        core::field("context_samples", &Value::context_samples, core::Count{}),
        core::field("corrected_samples", &Value::corrected_samples, core::Count{}),
        core::field("pre_clamp", &Value::pre_clamp),
        core::field("clipped_low_samples", &Value::clipped_low_samples, core::Count{}),
        core::field("clipped_high_samples", &Value::clipped_high_samples, core::Count{}),
    };
};
} // namespace docenhance
