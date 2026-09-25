// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/color/converter.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/surface.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <utility>
namespace docenhance::host {
struct IlluminationRun {
    std::reference_wrapper<const methods::SurfaceModel> model;
    image::PlaneView<const std::uint8_t> protection;
    std::reference_wrapper<methods::IlluminationReport> report;
    std::reference_wrapper<const core::Cancellation> cancellation;
};
class IlluminationRows final : public image::RowSource {
  public:
    IlluminationRows(color::Converter& source, image::Plane<double> block, IlluminationRun run)
        : source_(source), block_(std::move(block)), run_(run) {}
    [[nodiscard]] image::OutputDescriptor descriptor() const noexcept override;
    [[nodiscard]] core::Result<void> row(std::uint32_t index, std::span<std::uint8_t> bytes,
                                         image::RowUse use) override;

  private:
    std::reference_wrapper<color::Converter> source_;
    image::Plane<double> block_;
    IlluminationRun run_;
};
} // namespace docenhance::host
