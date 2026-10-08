// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/restoration.hpp"

#include <functional>
#include <span>
namespace docenhance::opencv {
struct RestorationExecution {
    RestorationExecution(core::Budget& b, core::Cancellation c, methods::RestorationReport& r,
                         image::RowUse use)
        : budget(b), cancellation(std::move(c)), report(r), preparation_use(use) {}
    std::reference_wrapper<core::Budget> budget;
    core::Cancellation cancellation;
    std::reference_wrapper<methods::RestorationReport> report;
    image::RowUse preparation_use;
};
[[nodiscard]] core::Result<methods::ResolvedPsf> resolve_psf(const methods::Wiener& method);
class RestorationModel {
  public:
    [[nodiscard]] static core::Result<RestorationModel>
    prepare(image::LinearSource& source, image::PlaneView<const std::uint8_t> mask,
            const methods::Wiener& method, methods::ResolvedPsf psf,
            const RestorationExecution& execution);
    [[nodiscard]] bool active() const noexcept {
        return !candidate_.empty();
    }
    [[nodiscard]] bool measured_source() const noexcept {
        return active();
    }
    [[nodiscard]] core::Result<void> apply(image::RowRange range, std::span<double> rgb,
                                           image::PlaneView<const std::uint8_t> mask,
                                           methods::RestorationReport& report,
                                           const core::Cancellation& cancellation) const;

  private:
    RestorationModel(image::Extent extent, methods::Wiener method, image::Plane<double> candidate)
        : extent_(extent), method_(std::move(method)), candidate_(std::move(candidate)) {}
    image::Extent extent_;
    methods::Wiener method_;
    image::Plane<double> candidate_;
};
} // namespace docenhance::opencv
