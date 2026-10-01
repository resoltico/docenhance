// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/color/converter.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/surface.hpp"

#include <functional>
#include <span>
namespace docenhance::host {
class IlluminatedSource final : public image::LinearSource {
  public:
    IlluminatedSource(color::Converter& source, const methods::SurfaceModel* model,
                      image::PlaneView<const std::uint8_t> protection,
                      methods::IlluminationReport& report, const core::Cancellation& cancellation)
        : source_(source), model_(model), protection_(protection), report_(report),
          cancellation_(cancellation) {}
    [[nodiscard]] image::Extent extent() const noexcept override {
        return source_.get().extent();
    }
    [[nodiscard]] core::Result<void> read(image::RowRange range, std::span<double> rgb,
                                          image::RowUse use) override;

  private:
    std::reference_wrapper<color::Converter> source_;
    const methods::SurfaceModel* model_;
    image::PlaneView<const std::uint8_t> protection_;
    std::reference_wrapper<methods::IlluminationReport> report_;
    std::reference_wrapper<const core::Cancellation> cancellation_;
};
class ContinuousRows final : public image::RowSource {
  public:
    ContinuousRows(image::LinearSource& source, image::OutputDescriptor descriptor,
                   image::Plane<double> block, bool prepared)
        : source_(source), descriptor_(descriptor), block_(std::move(block)), prepared_(prepared) {}
    [[nodiscard]] image::OutputDescriptor descriptor() const noexcept override {
        return descriptor_;
    }
    [[nodiscard]] core::Result<void> row(std::uint32_t index, std::span<std::uint8_t> bytes,
                                         image::RowUse use) override;

  private:
    std::reference_wrapper<image::LinearSource> source_;
    image::OutputDescriptor descriptor_;
    image::Plane<double> block_;
    bool prepared_;
};
} // namespace docenhance::host
