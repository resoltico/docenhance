// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"

#include <functional>
namespace docenhance::host {
struct DenoisingPlanes {
    image::Plane<std::uint16_t> input;
    image::Plane<std::uint16_t> output;
};
class DenoisedSource final : public image::LinearSource {
  public:
    DenoisedSource(image::LinearSource& source, const DenoisingPlanes& planes,
                   image::PlaneView<const std::uint8_t> protection,
                   methods::DenoisingReport& report)
        : source_(source), planes_(planes), protection_(protection), report_(report) {}
    DenoisedSource(image::LinearSource&, const DenoisingPlanes&&,
                   image::PlaneView<const std::uint8_t>, methods::DenoisingReport&) = delete;
    [[nodiscard]] image::Extent extent() const noexcept override {
        return source_.get().extent();
    }
    [[nodiscard]] core::Result<void> read(image::RowRange range, std::span<double> rgb,
                                          image::RowUse use) override;

  private:
    std::reference_wrapper<image::LinearSource> source_;
    std::reference_wrapper<const DenoisingPlanes> planes_;
    image::PlaneView<const std::uint8_t> protection_;
    std::reference_wrapper<methods::DenoisingReport> report_;
};
[[nodiscard]] core::Result<DenoisingPlanes>
prepare_denoising(image::LinearSource& source, const app::ProcessRequest& request,
                  core::Budget& budget, const core::Cancellation& cancellation,
                  methods::DenoisingReport& report);
[[nodiscard]] core::Result<void> assess_denoising(DenoisedSource& source, core::Budget& budget,
                                                  const core::Cancellation& cancellation,
                                                  methods::DenoisingReport& report);
} // namespace docenhance::host
