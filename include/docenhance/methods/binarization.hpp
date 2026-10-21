// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/method_catalog.hpp"
#include "docenhance/methods/parameters.hpp"

#include <cstddef>
#include <cstdint>
#include <variant>

namespace docenhance::methods {
inline constexpr std::uint32_t otsu_histogram_bins = 4096;
inline constexpr std::uint16_t otsu_fallback_threshold = (otsu_histogram_bins - 1) / 2;
inline constexpr std::uint16_t otsu_max_threshold = otsu_histogram_bins - 2;
inline constexpr std::size_t otsu_scratch_bytes = otsu_histogram_bins * sizeof(std::uint64_t);
class FixedThreshold {
  public:
    [[nodiscard]] static core::Result<FixedThreshold>
    create(double threshold = FixedThresholdParameters{}.threshold);
    [[nodiscard]] double threshold() const noexcept {
        return threshold_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return fixed_descriptor;
    }

  private:
    explicit FixedThreshold(double threshold) noexcept : threshold_(threshold) {}
    double threshold_;
};
class Sauvola {
  public:
    [[nodiscard]] static core::Result<Sauvola> create(SauvolaParameters parameters = {});
    [[nodiscard]] std::uint32_t window() const noexcept {
        return parameters_.window;
    }
    [[nodiscard]] double k() const noexcept {
        return parameters_.k;
    }
    [[nodiscard]] double r() const noexcept {
        return parameters_.r;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return sauvola_descriptor;
    }

  private:
    explicit Sauvola(SauvolaParameters parameters) noexcept : parameters_(parameters) {}
    SauvolaParameters parameters_;
};
class Otsu {
  public:
    [[nodiscard]] static Otsu create() noexcept {
        return Otsu{};
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return otsu_descriptor;
    }

  private:
    Otsu() noexcept = default;
};
using Binarization = std::variant<Sauvola, FixedThreshold, Otsu>;
[[nodiscard]] ImplementedMethod describe(const Binarization& method);
} // namespace docenhance::methods
