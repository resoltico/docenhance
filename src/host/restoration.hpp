// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/opencv/restoration.hpp"

#include <functional>
#include <optional>
#include <span>
namespace docenhance::host {
struct RestoreInput {
    std::reference_wrapper<image::LinearSource> source;
    image::PlaneView<const std::uint8_t> mask;
    bool prepared = false;
    bool geometry_transformed = false;
    std::reference_wrapper<const methods::IlluminationReport> illumination;
    std::reference_wrapper<const methods::DenoisingReport> denoising;
};
[[nodiscard]] core::Result<std::optional<opencv::RestorationModel>>
prepare_restoration(RestoreInput input, const methods::Restoration& method, core::Budget& budget,
                    const core::Cancellation& cancellation, methods::RestorationReport& report);
class RestoredSource final : public image::LinearSource {
  public:
    RestoredSource(RestoreInput input, const opencv::RestorationModel* model,
                   methods::RestorationReport& report, core::Cancellation cancellation)
        : input_(input), model_(model), report_(report), cancellation_(std::move(cancellation)) {}
    [[nodiscard]] image::Extent extent() const noexcept override {
        return input_.source.get().extent();
    }
    [[nodiscard]] bool active() const noexcept {
        return model_ != nullptr && model_->active();
    }
    [[nodiscard]] bool prepared() const noexcept {
        return input_.prepared || (model_ != nullptr && model_->measured_source());
    }
    [[nodiscard]] core::Result<void> read(image::RowRange range, std::span<double> rgb,
                                          image::RowUse use) override;

  private:
    RestoreInput input_;
    const opencv::RestorationModel* model_;
    std::reference_wrapper<methods::RestorationReport> report_;
    core::Cancellation cancellation_;
};
[[nodiscard]] core::Result<void> assess_restoration(RestoredSource& source, core::Budget& budget,
                                                    const core::Cancellation& cancellation,
                                                    methods::RestorationReport& report);
} // namespace docenhance::host
