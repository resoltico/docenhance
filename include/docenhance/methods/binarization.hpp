// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/method_catalog.hpp"
#include "docenhance/methods/parameters.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <variant>

namespace docenhance::methods {
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
