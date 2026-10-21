// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/working.hpp"
#include "nlm_detail.hpp"
#include "nlm_math.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>
namespace docenhance::methods {
namespace {
core::Result<NlmPrepared> allocate_shared(const Nlm& method, Working working) {
    const auto p = method.parameters();
    const auto radius = (p.patch + p.search - 2) / 2;
    const auto width = working.luminance.width();
    const auto height = working.luminance.height();
    auto codes = nlm_allocate<std::uint16_t>(working, width, height);
    if (!codes) {
        return std::unexpected(std::move(codes.error()));
    }
    auto weights = nlm_allocate<double>(working, nlm_weight_entries(method), 1);
    if (!weights) {
        return std::unexpected(std::move(weights.error()));
    }
    auto xfold = nlm_allocate<std::uint32_t>(working, width + (2 * radius), 1);
    if (!xfold) {
        return std::unexpected(std::move(xfold.error()));
    }
    auto yfold = nlm_allocate<std::uint32_t>(working, height + (2 * radius), 1);
    if (!yfold) {
        return std::unexpected(std::move(yfold.error()));
    }
    auto observations =
        nlm_allocate<std::uint64_t>(working, nlm_band_count(height), nlm_observation_rows);
    if (!observations) {
        return std::unexpected(std::move(observations.error()));
    }
    return NlmPrepared{
        .codes = std::move(*codes),
        .weights = std::move(*weights),
        .xfold = std::move(*xfold),
        .yfold = std::move(*yfold),
        .observations = std::move(*observations),
    };
}
core::Result<void> fill_fold(Working working, image::PlaneView<std::uint32_t> fold,
                             std::uint32_t extent, std::uint32_t radius) {
    const auto row = fold.row(0);
    const auto cancellation = working.scheduler.get().cancellation();
    for (std::uint32_t i = 0; i < row.size(); ++i) {
        if (i % nlm_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::initialization)) {
            return core::cancelled();
        }
        row.subspan(i, 1).front() = static_cast<std::uint32_t>(
            image::reflect101_folded(static_cast<std::int64_t>(i) - radius, extent));
    }
    return {};
}
core::Result<void> fill_weights(const Nlm& method, Working working,
                                image::PlaneView<double> weights) {
    const auto p = method.parameters();
    const double n = p.patch * p.patch;
    const double eta = p.h / image::byte_max;
    const double normalization = n * image::word_max * eta;
    const auto row = weights.row(0);
    const auto cancellation = working.scheduler.get().cancellation();
    for (std::uint32_t i = 0; i < row.size(); ++i) {
        if (i % nlm_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::initialization)) {
            return core::cancelled();
        }
        row.subspan(i, 1).front() = nlm_weight(i, normalization);
    }
    return {};
}
core::Result<void> gather_codes(Working working, image::PlaneView<std::uint16_t> codes,
                                NlmBand band) {
    const auto cancellation = working.scheduler.get().cancellation();
    for (auto y = band.first; y < band.first + band.rows; ++y) {
        const auto source = working.luminance.row(y);
        const auto row = codes.row(y);
        for (std::uint32_t x = 0; x < source.size(); ++x) {
            if (x % nlm_checkpoint_samples == 0 &&
                cancellation.requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            row.subspan(x, 1).front() = nlm_code(image::srgb_encode(source.subspan(x, 1).front()));
        }
    }
    return {};
}
} // namespace
core::Result<NlmPrepared> prepare_nlm(const Nlm& method, Working working) {
    auto prepared = allocate_shared(method, working);
    if (!prepared) {
        return std::unexpected(std::move(prepared.error()));
    }
    const auto p = method.parameters();
    const auto radius = (p.patch + p.search - 2) / 2;
    if (auto filled = fill_fold(working, prepared->xfold.view(), prepared->codes.width(), radius);
        !filled) {
        return std::unexpected(std::move(filled.error()));
    }
    if (auto filled = fill_fold(working, prepared->yfold.view(), prepared->codes.height(), radius);
        !filled) {
        return std::unexpected(std::move(filled.error()));
    }
    const auto table = [&](std::size_t) {
        return fill_weights(method, working, prepared->weights.view());
    };
    if (auto filled = working.scheduler.get().for_each(1, exec::WorkRef{table}); !filled) {
        return std::unexpected(std::move(filled.error()));
    }
    const auto gather = [&](std::size_t index) {
        return gather_codes(working, prepared->codes.view(),
                            nlm_band(prepared->codes.height(), static_cast<std::uint32_t>(index)));
    };
    if (auto gathered = working.scheduler.get().for_each(nlm_band_count(prepared->codes.height()),
                                                         exec::WorkRef{gather});
        !gathered) {
        return std::unexpected(std::move(gathered.error()));
    }
    return std::move(*prepared);
}
} // namespace docenhance::methods
