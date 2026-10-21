// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/report_values.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <utility>

namespace docenhance::methods {
enum class NlmReason : std::uint8_t { none, zero_blend, no_eligible_samples, no_effect };
struct NlmFacts {
    Disposition disposition = Disposition::no_change;
    NlmReason reason = NlmReason::no_eligible_samples;
    PixelCounts counts{};
    std::uint64_t corrected_samples{};
    std::uint32_t weight_table_entries{};
    bool operator==(const NlmFacts&) const = default;
};
// The v2 cutoff's literal evaluation order. The same size owns admission, charge and lookup.
[[nodiscard]] std::uint32_t nlm_weight_entries(const Nlm& method) noexcept;
[[nodiscard]] bool validate(const NlmFacts& facts) noexcept;
[[nodiscard]] bool validate(const NlmFacts& facts, NlmParameters parameters) noexcept;
} // namespace docenhance::methods

namespace docenhance {
template <> struct core::EnumNames<methods::NlmReason> {
    using Value = methods::NlmReason;
    static constexpr std::array list{
        std::pair{Value::none, std::string_view{"none"}},
        std::pair{Value::zero_blend, std::string_view{"zero_blend"}},
        std::pair{Value::no_eligible_samples, std::string_view{"no_eligible_samples"}},
        std::pair{Value::no_effect, std::string_view{"no_effect"}},
    };
};
template <> struct core::WireTag<methods::NlmFacts> {
    static constexpr std::string_view key = "method";
    static constexpr auto value = methods::Nlm::descriptor().id;
};
template <> struct core::Fields<methods::NlmFacts> {
    using Value = methods::NlmFacts;
    static constexpr auto list = std::tuple{
        core::field("disposition", &Value::disposition),
        core::field("reason", &Value::reason),
        core::field("counts", &Value::counts),
        core::field("corrected_samples", &Value::corrected_samples, core::Count{}),
        core::field("weight_table_entries", &Value::weight_table_entries, core::Count{}),
    };
};
} // namespace docenhance
