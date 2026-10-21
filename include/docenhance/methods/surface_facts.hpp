// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameter_domains.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/surface.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace docenhance::methods {
enum class SurfaceReason : std::uint8_t {
    none,
    no_eligible_samples,
    zero_strength,
    unit_gain,
    insufficient_samples,
    insufficient_cells,
    automatic_predicates,
    no_effect,
};
struct SurfaceFacts {
    Disposition disposition = Disposition::no_change;
    SurfaceReason reason = SurfaceReason::no_eligible_samples;
    PixelCounts counts{};
    std::optional<std::uint32_t> resolved_cell = std::nullopt;
    std::uint32_t cells{};
    std::uint32_t measured_cells{};
    std::uint32_t dark_cells{};
    std::optional<double> background_reference = std::nullopt;
    std::optional<double> coverage = std::nullopt;
    std::optional<SolverReport> solver = std::nullopt;
    std::optional<SurfaceMeasurements> measurements = std::nullopt;
    SurfacePredicates auto_predicates{};
    std::uint64_t gain_capped_samples{};
    std::uint64_t saturated_samples{};
    double min_gain = 1;
    double max_gain = 1;
    bool operator==(const SurfaceFacts&) const = default;
};
[[nodiscard]] bool validate(const SurfaceFacts& facts) noexcept;
[[nodiscard]] bool validate(const SurfaceFacts& facts, SurfaceParameters parameters,
                            image::Extent extent) noexcept;
} // namespace docenhance::methods

namespace docenhance {
template <> struct core::EnumNames<methods::SurfaceReason> {
    using Value = methods::SurfaceReason;
    static constexpr std::array list{
        std::pair{Value::none, std::string_view{"none"}},
        std::pair{Value::no_eligible_samples, std::string_view{"no_eligible_samples"}},
        std::pair{Value::zero_strength, std::string_view{"zero_strength"}},
        std::pair{Value::unit_gain, std::string_view{"unit_gain"}},
        std::pair{Value::insufficient_samples, std::string_view{"insufficient_samples"}},
        std::pair{Value::insufficient_cells, std::string_view{"insufficient_cells"}},
        std::pair{Value::automatic_predicates, std::string_view{"automatic_predicates"}},
        std::pair{Value::no_effect, std::string_view{"no_effect"}},
    };
};
template <> struct core::WireTag<methods::SurfaceFacts> {
    static constexpr std::string_view key = "method";
    static constexpr auto value = methods::Surface::descriptor().id;
};
template <> struct core::Fields<methods::SurfaceFacts> {
    using Value = methods::SurfaceFacts;
    static constexpr auto cells = core::Range<std::uint32_t>{
        .minimum = 0,
        .maximum = methods::surface_cell_limit,
    };
    static constexpr auto list = std::tuple{
        core::field("disposition", &Value::disposition),
        core::field("reason", &Value::reason),
        core::field("counts", &Value::counts),
        core::field("resolved_cell", &Value::resolved_cell, methods::background_cell_domain),
        core::field("cells", &Value::cells, cells),
        core::field("measured_cells", &Value::measured_cells, cells),
        core::field("dark_cells", &Value::dark_cells, cells),
        core::field("background_reference", &Value::background_reference, core::Unit{}),
        core::field("coverage", &Value::coverage, core::Unit{}),
        core::field("solver", &Value::solver),
        core::field("measurements", &Value::measurements),
        core::field("auto_predicates", &Value::auto_predicates),
        core::field("gain_capped_samples", &Value::gain_capped_samples, core::Count{}),
        core::field("saturated_samples", &Value::saturated_samples, core::Count{}),
        core::field("min_gain", &Value::min_gain, methods::background_max_gain_domain),
        core::field("max_gain", &Value::max_gain, methods::background_max_gain_domain),
    };
};
} // namespace docenhance
