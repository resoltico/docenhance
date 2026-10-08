// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/opencv/restoration.hpp"
#include "restoration_work.hpp"

#include <cmath>
#include <cstdint>
#include <expected>
#include <utility>
namespace docenhance::opencv {
namespace {
core::Result<void> count(image::Extent extent, image::PlaneView<const std::uint8_t> mask,
                         const RestorationExecution& e) {
    auto& r = e.report.get();
    for (std::uint32_t y = 0; y < mask.height(); ++y) {
        for (std::uint32_t x = 0; x < mask.width(); ++x) {
            constexpr std::uint32_t interval = 1024;
            if (x % interval == 0 && e.cancellation.requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            r.protected_samples +=
                static_cast<std::uint64_t>(mask.row(y).subspan(x, 1).front() != 0);
        }
    }
    r.eligible_samples = (std::uint64_t{extent.width} * extent.height) - r.protected_samples;
    if (e.cancellation.requested(core::Checkpoint::measurement)) {
        return core::cancelled();
    }
    return {};
}
} // namespace
core::Result<RestorationModel> RestorationModel::prepare(image::LinearSource& source,
                                                         image::PlaneView<const std::uint8_t> mask,
                                                         const methods::Wiener& method,
                                                         methods::ResolvedPsf psf,
                                                         const RestorationExecution& e) {
    auto& r = e.report.get();
    r = {};
    r.requested = method.parameters();
    r.status = methods::RestorationStatus::failed;
    r.reason = methods::RestorationReason::processing_failure;
    r.inference_warning = true;
    auto validated = methods::validate_psf(psf);
    if (!validated) {
        return std::unexpected(validated.error());
    }
    if (!methods::canonical_psf(method.parameters(), psf)) {
        return core::failure(core::ErrorCode::argument,
                             "Resolved PSF does not match requested model");
    }
    r.off_center_warning =
        std::hypot(psf.centroid_x, psf.centroid_y) > methods::psf_off_center_limit;
    r.psf = std::move(psf);
    const auto extent = source.extent();
    const auto total = std::uint64_t{extent.width} * extent.height;
    if (extent.width == 0 || extent.height == 0 || total > image::source_pixels_max ||
        (!mask.empty() && (mask.width() != extent.width || mask.height() != extent.height))) {
        return core::failure(core::ErrorCode::argument,
                             "Invalid restoration source or mask extent");
    }
    auto counted = count(extent, mask, e);
    if (!counted) {
        return std::unexpected(counted.error());
    }
    if (method.parameters().blend == 0 || r.eligible_samples == 0) {
        r.status = methods::RestorationStatus::no_change;
        r.reason = method.parameters().blend == 0 ? methods::RestorationReason::zero_blend
                                                  : methods::RestorationReason::no_eligible_samples;
        r.complete = true;
        return RestorationModel{extent, method, {}};
    }
    auto work = restoration_work(extent, *r.psf, e);
    if (!work) {
        return std::unexpected(work.error());
    }
    auto gathered = restoration_gather(source, *work, e);
    if (!gathered) {
        return std::unexpected(gathered.error());
    }
    auto padded = restoration_pad(*work, *r.psf, e);
    if (!padded) {
        return std::unexpected(padded.error());
    }
    auto transformed = restoration_transforms(*work, method, e);
    if (!transformed) {
        return std::unexpected(transformed.error());
    }
    return RestorationModel{extent, method, std::move(work->candidate)};
}
} // namespace docenhance::opencv
