// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/restoration_working.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fft/fft.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/working.hpp"
#include "restoration_working_detail.hpp"

#include <expected>
#include <utility>
namespace docenhance::methods {
namespace {
core::Result<WienerFacts> completed(const Wiener& method, Working working, WienerFacts facts) {
    if (!validate(facts, method.parameters(),
                  {.width = working.luminance.width(), .height = working.luminance.height()})) {
        return core::failure(core::ErrorCode::invariant,
                             "Restoration produced invalid completed facts");
    }
    return facts;
}
core::Result<void> solve(Working working, const Wiener& method, const ResolvedPsf& psf,
                         image::PlaneView<double> frame) {
    auto signal = fft::forward(frame.as_const(), working.budget.get(), working.scheduler.get());
    if (!signal) {
        return std::unexpected(std::move(signal.error()));
    }
    if (auto placed = restoration_kernel(working, frame, psf); !placed) {
        return placed;
    }
    {
        auto kernel = fft::forward(frame.as_const(), working.budget.get(), working.scheduler.get());
        if (!kernel) {
            return std::unexpected(std::move(kernel.error()));
        }
        if (auto filtered = restoration_filter(signal->storage(), kernel->storage(),
                                               signal->shape(), method, working.scheduler.get());
            !filtered) {
            return filtered;
        }
    } // Kernel spectrum refunds BEFORE the inverse's const-preserving work copy.
    return fft::inverse(*signal, frame, working.budget.get(), working.scheduler.get());
}
core::Result<WienerFacts> execute(const Wiener& method, const ResolvedPsf& psf, Working working,
                                  WienerFacts facts) {
    auto geometry = restoration_geometry(working, psf);
    if (!geometry) {
        return std::unexpected(std::move(geometry.error()));
    }
    if (working.scheduler.get().cancellation().requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto frame =
        image::Plane<double>::allocate(working.budget.get(), geometry->width, geometry->height);
    if (!frame) {
        return std::unexpected(std::move(frame.error()));
    }
    auto mean = restoration_mean(working, frame->view(), *geometry);
    if (!mean) {
        return std::unexpected(std::move(mean.error()));
    }
    if (auto centered = restoration_center(working, frame->view(), *mean); !centered) {
        return std::unexpected(std::move(centered.error()));
    }
    if (auto solved = solve(working, method, psf, frame->view()); !solved) {
        return std::unexpected(std::move(solved.error()));
    }
    facts.context_samples = facts.counts.eligible + facts.counts.protected_samples;
    facts.transform = RestorationTransform{
        .guard = geometry->guard,
        .width = geometry->width,
        .height = geometry->height,
        .padded_mean = *mean,
    };
    auto mapped =
        restoration_map(working, method, frame->view().as_const(), *facts.transform, facts);
    if (!mapped) {
        return mapped;
    }
    return completed(method, working, *mapped);
}
} // namespace
core::Result<WienerFacts> apply(const Wiener& method, const ResolvedPsf& psf, Working working) {
    auto scalars = restoration_psf_facts(method, psf);
    if (!scalars) {
        return std::unexpected(std::move(scalars.error()));
    }
    auto counts = eligibility(working);
    if (!counts) {
        return std::unexpected(std::move(counts.error()));
    }
    WienerFacts facts{.counts = *counts, .psf = *scalars};
    if (method.parameters().blend == 0) {
        facts.reason = WienerReason::zero_blend;
        return completed(method, working, facts);
    }
    if (counts->eligible == 0) {
        return completed(method, working, facts);
    }
    return execute(method, psf, working, facts);
}
} // namespace docenhance::methods
