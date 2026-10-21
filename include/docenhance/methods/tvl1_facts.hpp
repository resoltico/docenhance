// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/tvl1.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace docenhance::methods {
enum class Tvl1Reason : std::uint8_t { none, zero_blend, no_eligible_samples, no_effect };
struct Tvl1Solution {
    std::uint32_t iterations{};
    Tvl1Stop stop = Tvl1Stop::iteration_limit;
    std::uint32_t passing_checkpoints{};
    double primal_update{};
    double dual_update{};
    double objective_start{};
    double objective_end{};
    bool operator==(const Tvl1Solution&) const = default;
};
struct Tvl1Facts {
    Disposition disposition = Disposition::no_change;
    Tvl1Reason reason = Tvl1Reason::no_eligible_samples;
    PixelCounts counts{};
    std::uint64_t context_samples{};
    std::uint64_t corrected_samples{};
    std::optional<Tvl1Solution> solver = std::nullopt;
    bool operator==(const Tvl1Facts&) const = default;
};
[[nodiscard]] bool validate(const Tvl1Solution& solution) noexcept;
[[nodiscard]] bool validate(const Tvl1Facts& facts) noexcept;
[[nodiscard]] bool validate(const Tvl1Facts& facts, Tvl1Parameters parameters) noexcept;
} // namespace docenhance::methods

namespace docenhance {
template <> struct core::EnumNames<methods::Tvl1Reason> {
    using Value = methods::Tvl1Reason;
    static constexpr std::array list{
        std::pair{Value::none, std::string_view{"none"}},
        std::pair{Value::zero_blend, std::string_view{"zero_blend"}},
        std::pair{Value::no_eligible_samples, std::string_view{"no_eligible_samples"}},
        std::pair{Value::no_effect, std::string_view{"no_effect"}},
    };
};
template <> struct core::WireTag<methods::Tvl1Facts> {
    static constexpr std::string_view key = "method";
    static constexpr auto value = methods::Tvl1::descriptor().id;
};
template <> struct core::Fields<methods::Tvl1Solution> {
    using Value = methods::Tvl1Solution;
    static constexpr auto nonnegative = core::Range<double>{
        .minimum = 0,
        .maximum = std::numeric_limits<double>::max(),
    };
    static constexpr auto list = std::tuple{
        core::field("iterations", &Value::iterations, methods::tv_iterations_domain),
        core::field("stop", &Value::stop),
        core::field(
            "passing_checkpoints", &Value::passing_checkpoints,
            core::Range<std::uint32_t>{.minimum = 0, .maximum = methods::tvl1_required_passes}),
        core::field("primal_update", &Value::primal_update, core::Unit{}),
        core::field("dual_update", &Value::dual_update,
                    core::Range<double>{.minimum = 0, .maximum = 2 + methods::tvl1_dual_roundoff}),
        core::field("objective_start", &Value::objective_start, nonnegative),
        core::field("objective_end", &Value::objective_end, nonnegative),
    };
};
template <> struct core::Fields<methods::Tvl1Facts> {
    using Value = methods::Tvl1Facts;
    static constexpr auto list = std::tuple{
        core::field("disposition", &Value::disposition),
        core::field("reason", &Value::reason),
        core::field("counts", &Value::counts),
        core::field("context_samples", &Value::context_samples, core::Count{}),
        core::field("corrected_samples", &Value::corrected_samples, core::Count{}),
        core::field("solver", &Value::solver),
    };
};
} // namespace docenhance
