// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/method_catalog.hpp"
#include "docenhance/methods/parameters.hpp"

#include <array>
#include <cstdint>
#include <tuple>
#include <variant>
namespace docenhance::methods {
inline constexpr double levels_minimum_range = 1e-6;
class Levels {
  public:
    [[nodiscard]] static core::Result<Levels> create(LevelsParameters p = {});
    [[nodiscard]] LevelsParameters parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return levels_descriptor;
    }

  private:
    explicit Levels(LevelsParameters p) : parameters_(p) {}
    LevelsParameters parameters_;
};
class Gamma {
  public:
    [[nodiscard]] static core::Result<Gamma> create(GammaParameters p = {});
    [[nodiscard]] GammaParameters parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return gamma_descriptor;
    }

  private:
    explicit Gamma(GammaParameters p) : parameters_(p) {}
    GammaParameters parameters_;
};
inline constexpr std::uint32_t clahe_minimum_tile = 16;
inline constexpr std::uint32_t clahe_minimum_samples = 16;
class Clahe {
  public:
    [[nodiscard]] static core::Result<Clahe> create(ClaheParameters p = {});
    [[nodiscard]] ClaheParameters parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return clahe_descriptor;
    }

  private:
    explicit Clahe(ClaheParameters p) : parameters_(p) {}
    ClaheParameters parameters_;
};
inline constexpr std::uint32_t clahe_bins = 1024;
inline constexpr std::uint32_t clahe_tiles_max =
    clahe_grid_columns_domain.maximum * clahe_grid_rows_domain.maximum;
struct ClaheMaps {
    image::Plane<double> knots;
    std::array<bool, clahe_tiles_max> identity{};
};
struct ContrastOff {};
using Contrast = std::variant<ContrastOff, Levels, Gamma, Clahe>;
struct LevelsRange {
    double low = 0;
    double high = 0;
    bool operator==(const LevelsRange&) const = default;
};
[[nodiscard]] constexpr bool validate(LevelsRange range) noexcept {
    return core::in_range(range.low, 0.0, 1.0) && core::in_range(range.high, 0.0, 1.0) &&
           range.low <= range.high;
}
// Inputs are finite unit f and an admitted active range with gap >= levels_minimum_range.
[[nodiscard]] double levels_candidate(double f, LevelsRange range) noexcept;
// Perceptual f is finite unit data admitted by its actual producer/receiving boundary.
[[nodiscard]] double gamma_candidate(double f, const Gamma& method) noexcept;
} // namespace docenhance::methods

namespace docenhance {
template <> struct core::Fields<methods::LevelsRange> {
    static constexpr auto list = std::tuple{
        core::field("low", &methods::LevelsRange::low, core::Unit{}),
        core::field("high", &methods::LevelsRange::high, core::Unit{}),
    };
};
} // namespace docenhance
