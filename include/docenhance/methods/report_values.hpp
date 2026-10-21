// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/image/source_facts.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <utility>

namespace docenhance::methods {
enum class Family : std::uint8_t {
    illumination,
    denoising,
    restoration,
    contrast,
    sharpening,
    binarization,
};
enum class Disposition : std::uint8_t { applied, no_change, skipped };
struct PixelCounts {
    std::uint64_t eligible{};
    std::uint64_t protected_samples{};
    std::uint64_t evaluated{};
    std::uint64_t changed{};
    bool operator==(const PixelCounts&) const = default;
};
[[nodiscard]] constexpr bool validate(const PixelCounts& counts) noexcept {
    return counts.eligible <= image::source_pixels_max &&
           counts.protected_samples <= image::source_pixels_max - counts.eligible &&
           counts.eligible + counts.protected_samples != 0 && counts.evaluated <= counts.eligible &&
           counts.changed <= counts.evaluated;
}
[[nodiscard]] constexpr bool counts_match(const PixelCounts& counts,
                                          std::uint64_t pixels) noexcept {
    return validate(counts) && counts.eligible + counts.protected_samples == pixels;
}
} // namespace docenhance::methods

namespace docenhance {
template <> struct core::Fields<methods::PixelCounts> {
    static constexpr auto list = std::tuple{
        core::field("eligible", &methods::PixelCounts::eligible, core::Count{}),
        core::field("protected", &methods::PixelCounts::protected_samples, core::Count{}),
        core::field("evaluated", &methods::PixelCounts::evaluated, core::Count{}),
        core::field("changed", &methods::PixelCounts::changed, core::Count{}),
    };
};
template <> struct core::EnumNames<methods::Disposition> {
    using Value = methods::Disposition;
    static constexpr std::array list{
        std::pair{Value::applied, std::string_view{"applied"}},
        std::pair{Value::no_change, std::string_view{"no_change"}},
        std::pair{Value::skipped, std::string_view{"skipped"}},
    };
};
template <> struct core::EnumNames<methods::Family> {
    using Value = methods::Family;
    static constexpr std::array list{
        std::pair{Value::illumination, std::string_view{"illumination"}},
        std::pair{Value::denoising, std::string_view{"denoising"}},
        std::pair{Value::restoration, std::string_view{"restoration"}},
        std::pair{Value::contrast, std::string_view{"contrast"}},
        std::pair{Value::sharpening, std::string_view{"sharpening"}},
        std::pair{Value::binarization, std::string_view{"binarization"}},
    };
};
} // namespace docenhance
