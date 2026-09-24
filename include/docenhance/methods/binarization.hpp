// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/method_catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <variant>

namespace docenhance::methods {
class FixedThreshold {
  public:
    static constexpr double default_threshold = 0.5;
    [[nodiscard]] static core::Result<FixedThreshold> create(double threshold = default_threshold);
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
struct SauvolaParameters {
    std::uint32_t window = 31;
    double k = 0.2;
    double r = 0.5;
};
class Sauvola {
  public:
    static constexpr std::uint32_t default_window = SauvolaParameters{}.window;
    static constexpr std::uint32_t min_window = 3;
    static constexpr std::uint32_t max_window = 4095;
    static constexpr double default_k = SauvolaParameters{}.k;
    static constexpr double default_r = SauvolaParameters{}.r;
    static constexpr double min_r = 1.0 / 255.0;
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
using Binarization = std::variant<Sauvola, FixedThreshold>;
[[nodiscard]] ImplementedMethod describe(const Binarization& method);
// Charged scratch for the actual number of slots, capped by the image's strip count.
[[nodiscard]] core::Result<std::size_t> scratch_bytes(const Binarization& method,
                                                      std::uint32_t width, unsigned workers);
struct BinarizationContext {
    std::reference_wrapper<const exec::Scheduler> scheduler;
    std::reference_wrapper<core::Budget> budget;
};
[[nodiscard]] core::Result<void> binarize(image::PlaneView<const std::uint8_t> source,
                                          image::PlaneView<std::uint8_t> destination,
                                          const Binarization& method, BinarizationContext context);
} // namespace docenhance::methods
