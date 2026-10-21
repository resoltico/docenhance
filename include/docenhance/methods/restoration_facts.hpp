// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/restoration.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace docenhance::methods {
enum class PsfKind : std::uint8_t { gaussian, motion, kernel };
enum class WienerReason : std::uint8_t { none, zero_blend, no_eligible_samples, no_effect };
struct PsfFacts {
    PsfKind kind = PsfKind::gaussian;
    std::uint32_t width{};
    std::uint32_t height{};
    double normalized_sum{};
    double centroid_x{};
    double centroid_y{};
    bool operator==(const PsfFacts&) const = default;
};
struct RestorationTransform {
    std::uint32_t guard{};
    std::uint32_t width{};
    std::uint32_t height{};
    double padded_mean{};
    bool operator==(const RestorationTransform&) const = default;
};
struct WienerFacts {
    Disposition disposition = Disposition::no_change;
    WienerReason reason = WienerReason::no_eligible_samples;
    PixelCounts counts{};
    PsfFacts psf{};
    std::uint64_t context_samples{};
    std::uint64_t corrected_samples{};
    std::optional<RestorationTransform> transform = std::nullopt;
    std::uint64_t raw_low_samples{};
    std::uint64_t raw_high_samples{};
    std::uint64_t blended_low_samples{};
    std::uint64_t blended_high_samples{};
    bool operator==(const WienerFacts&) const = default;
};
[[nodiscard]] bool validate(const PsfFacts& facts) noexcept;
[[nodiscard]] bool validate(const RestorationTransform& transform) noexcept;
[[nodiscard]] bool validate(const WienerFacts& facts) noexcept;
[[nodiscard]] bool validate(const WienerFacts& facts, const WienerParameters& parameters,
                            image::Extent extent) noexcept;
} // namespace docenhance::methods

namespace docenhance {
template <> struct core::EnumNames<methods::PsfKind> {
    using Value = methods::PsfKind;
    static constexpr std::array list{
        std::pair{Value::gaussian, std::string_view{"gaussian"}},
        std::pair{Value::motion, std::string_view{"motion"}},
        std::pair{Value::kernel, std::string_view{"kernel"}},
    };
};
template <> struct core::EnumNames<methods::WienerReason> {
    using Value = methods::WienerReason;
    static constexpr std::array list{
        std::pair{Value::none, std::string_view{"none"}},
        std::pair{Value::zero_blend, std::string_view{"zero_blend"}},
        std::pair{Value::no_eligible_samples, std::string_view{"no_eligible_samples"}},
        std::pair{Value::no_effect, std::string_view{"no_effect"}},
    };
};
template <> struct core::WireTag<methods::WienerFacts> {
    static constexpr std::string_view key = "method";
    static constexpr auto value = methods::Wiener::descriptor().id;
};
template <> struct core::Fields<methods::PsfFacts> {
    using Value = methods::PsfFacts;
    static constexpr auto side = core::Range<std::uint32_t>{
        .minimum = methods::psf_minimum_side,
        .maximum = methods::psf_maximum_side,
    };
    static constexpr auto sum = core::Range<double>{
        .minimum = 1 - methods::psf_normalization_tolerance,
        .maximum = 1 + methods::psf_normalization_tolerance,
    };
    static constexpr auto centroid = core::Range<double>{
        .minimum = -static_cast<double>(methods::psf_maximum_side),
        .maximum = methods::psf_maximum_side,
    };
    static constexpr auto list = std::tuple{
        core::field("kind", &Value::kind),
        core::field("width", &Value::width, side),
        core::field("height", &Value::height, side),
        core::field("normalized_sum", &Value::normalized_sum, sum),
        core::field("centroid_x", &Value::centroid_x, centroid),
        core::field("centroid_y", &Value::centroid_y, centroid),
    };
};
template <> struct core::Fields<methods::RestorationTransform> {
    using Value = methods::RestorationTransform;
    static constexpr auto extent = core::Range<std::uint32_t>{
        .minimum = 1,
        .maximum = methods::restoration_fft_maximum,
    };
    static constexpr auto list = std::tuple{
        core::field("guard", &Value::guard,
                    core::Range<std::uint32_t>{
                        .minimum = methods::restoration_minimum_guard,
                        .maximum = methods::fft_max_guard,
                    }),
        core::field("width", &Value::width, extent),
        core::field("height", &Value::height, extent),
        core::field("padded_mean", &Value::padded_mean, core::Unit{}),
    };
};
template <> struct core::Fields<methods::WienerFacts> {
    using Value = methods::WienerFacts;
    static constexpr auto list = std::tuple{
        core::field("disposition", &Value::disposition),
        core::field("reason", &Value::reason),
        core::field("counts", &Value::counts),
        core::field("psf", &Value::psf),
        core::field("context_samples", &Value::context_samples, core::Count{}),
        core::field("corrected_samples", &Value::corrected_samples, core::Count{}),
        core::field("transform", &Value::transform),
        core::field("raw_low_samples", &Value::raw_low_samples, core::Count{}),
        core::field("raw_high_samples", &Value::raw_high_samples, core::Count{}),
        core::field("blended_low_samples", &Value::blended_low_samples, core::Count{}),
        core::field("blended_high_samples", &Value::blended_high_samples, core::Count{}),
    };
};
} // namespace docenhance
