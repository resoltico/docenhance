// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
namespace docenhance::tests {
class LinearFixture final : public image::LinearSource {
  public:
    LinearFixture(core::Budget& budget, image::Extent extent)
        : extent_(extent),
          pixels_(image::Plane<double>::allocate(
                      budget, static_cast<std::uint32_t>(image::rgb_channels * extent.width),
                      extent.height)
                      .value()) {}
    [[nodiscard]] image::Extent extent() const noexcept override {
        return extent_;
    }
    [[nodiscard]] std::span<double> row(std::uint32_t y) {
        return pixels_.view().row(y);
    }
    void fill(double value) {
        std::ranges::fill(pixels_.view().storage(), value);
    }
    core::Result<void> read(image::RowRange range, std::span<double> samples,
                            image::RowUse /*use*/) override {
        if (range.row >= extent_.height || range.first >= extent_.width || samples.empty() ||
            samples.size() % image::rgb_channels != 0 ||
            samples.size() / image::rgb_channels > extent_.width - range.first) {
            return core::failure(core::ErrorCode::argument, "Bad linear fixture range");
        }
        std::ranges::copy(
            row(range.row).subspan(std::size_t{range.first} * image::rgb_channels, samples.size()),
            samples.begin());
        return {};
    }

  private:
    image::Extent extent_;
    image::Plane<double> pixels_;
};
} // namespace docenhance::tests
