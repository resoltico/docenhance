// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "contrast_detail.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/contrast.hpp"

#include <cstdint>
#include <expected>
#include <type_traits>
#include <variant>
namespace docenhance::methods {
namespace {
core::Result<void> count(image::Extent extent, image::PlaneView<const std::uint8_t> mask,
                         const ContrastExecution& e) {
    if (extent.width == 0 || extent.height == 0 ||
        (!mask.empty() && (mask.width() != extent.width || mask.height() != extent.height))) {
        return core::failure(core::ErrorCode::argument,
                             "Invalid contrast source or protection extent");
    }
    const auto total = std::uint64_t{extent.width} * extent.height;
    if (total > image::source_pixels_max) {
        return core::failure(core::ErrorCode::resource, "Contrast pixel limit exceeded");
    }
    std::uint64_t protected_count = 0;
    for (std::uint32_t y = 0; y < mask.height(); ++y) {
        const auto row = mask.row(y);
        for (std::uint32_t x = 0; x < mask.width(); ++x) {
            constexpr std::uint32_t interval = 1024;
            if (x % interval == 0 && e.cancellation.requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            protected_count += static_cast<std::uint64_t>(row.subspan(x, 1).front() != 0);
        }
    }
    e.report.get().protected_samples = protected_count;
    e.report.get().eligible_samples = total - protected_count;
    return {};
}
} // namespace
core::Result<ContrastModel> ContrastModel::prepare(image::LinearSource& source,
                                                   image::PlaneView<const std::uint8_t> mask,
                                                   const Contrast& method,
                                                   const ContrastExecution& e) {
    auto& r = e.report.get();
    r = {};
    std::visit(
        [&](const auto& selected) {
            using M = std::decay_t<decltype(selected)>;
            if constexpr (!std::is_same_v<M, ContrastOff>) {
                r.requested = selected.parameters();
                r.status = ContrastStatus::failed;
            }
        },
        method);
    if (e.cancellation.requested(core::Checkpoint::measurement)) {
        return core::cancelled();
    }
    auto counted = count(source.extent(), mask, e);
    if (!counted) {
        return std::unexpected(counted.error());
    }
    if (!r.requested) {
        r.complete = true;
        return ContrastModel{source.extent(), method, {}, false, false};
    }
    const auto blend = std::visit([](const auto& p) { return p.blend; }, *r.requested);
    if (blend == 0 || r.eligible_samples == 0) {
        r.status = ContrastStatus::no_change;
        r.reason = blend == 0 ? ContrastReason::zero_blend : ContrastReason::no_eligible_samples;
        r.complete = true;
        return ContrastModel{source.extent(), method, {}, false, false};
    }
    if (const auto* const gamma = std::get_if<Gamma>(&method);
        gamma != nullptr && gamma->parameters().gamma == 1) {
        r.status = ContrastStatus::no_change;
        r.reason = ContrastReason::identity_gamma;
        r.complete = true;
        return ContrastModel{source.extent(), method, {}, false, false};
    }
    if (const auto* const levels = std::get_if<Levels>(&method)) {
        auto range = measure_levels(source, mask, *levels, e);
        if (!range) {
            return std::unexpected(range.error());
        }
        r.levels = *range;
        if (range->high - range->low < levels_minimum_range) {
            r.status = ContrastStatus::no_change;
            r.reason = ContrastReason::insufficient_dynamic_range;
            r.complete = true;
            return ContrastModel{source.extent(), method, *range, false, true};
        }
        return ContrastModel{source.extent(), method, *range, true, true};
    }
    return ContrastModel{source.extent(), method, {}, true, false};
}
} // namespace docenhance::methods
