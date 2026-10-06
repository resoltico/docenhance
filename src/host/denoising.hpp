// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/tvl1.hpp"

#include <functional>
#include <utility>
#include <variant>
namespace docenhance::host {
struct NlmPlanes {
    image::Plane<std::uint16_t> input;
    image::Plane<std::uint16_t> output;
};
using DenoisingPlanes = std::variant<NlmPlanes, methods::Tvl1Model>;
[[nodiscard]] bool active_denoising(const DenoisingPlanes& planes);
class DenoisedSource final : public image::LinearSource {
  public:
    DenoisedSource(image::LinearSource& source, const DenoisingPlanes& planes,
                   image::PlaneView<const std::uint8_t> protection,
                   methods::DenoisingReport& report, core::Cancellation cancellation)
        : source_(source), planes_(planes), protection_(protection), report_(report),
          cancellation_(std::move(cancellation)) {}
    DenoisedSource(image::LinearSource&, const DenoisingPlanes&&,
                   image::PlaneView<const std::uint8_t>, methods::DenoisingReport&,
                   core::Cancellation) = delete;
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
    core::Cancellation cancellation_;
};
struct DenoisingInput {
    std::reference_wrapper<image::LinearSource> source;
    image::PlaneView<const std::uint8_t> protection;
    DenoisingInput(image::LinearSource& value, image::PlaneView<const std::uint8_t> mask)
        : source(value), protection(mask) {}
};
[[nodiscard]] core::Result<DenoisingPlanes>
prepare_denoising(DenoisingInput source_input, const app::ProcessRequest& request,
                  core::Budget& budget, const core::Cancellation& cancellation,
                  methods::DenoisingReport& report);
[[nodiscard]] core::Result<void> assess_denoising(DenoisedSource& source, core::Budget& budget,
                                                  const core::Cancellation& cancellation,
                                                  methods::DenoisingReport& report);
} // namespace docenhance::host
