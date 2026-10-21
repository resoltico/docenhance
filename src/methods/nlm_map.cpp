// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "nlm_detail.hpp"
#include "nlm_math.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::methods {
namespace {
core::Result<void> map_row(const NlmContext& context, NlmBand band, const NlmBandStorage& storage,
                           std::uint32_t local, NlmObservation& observation) {
    const auto y = band.first + local;
    const auto destination = context.working.luminance.row(y);
    const auto codes = context.prepared.get().codes.view().row(y);
    const auto numerator = storage.accumulation.view().row(local);
    const auto denominator = storage.accumulation.view().row(local + band.rows);
    const auto mask = context.working.protection.empty() ? std::span<const std::uint8_t>{}
                                                         : context.working.protection.row(y);
    const auto blend = context.method.get().parameters().blend;
    const auto cancellation = context.working.scheduler.get().cancellation();
    for (std::uint32_t x = 0; x < destination.size(); ++x) {
        if (x % nlm_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        if (!mask.empty() && mask.subspan(x, 1).front() != 0) {
            continue;
        }
        ++observation.evaluated;
        const auto code = codes.subspan(x, 1).front();
        const auto mean = numerator.subspan(x, 1).front() / denominator.subspan(x, 1).front();
        if (mean == static_cast<double>(code)) {
            continue;
        }
        ++observation.corrected;
        auto& value = destination.subspan(x, 1).front();
        const auto f = image::srgb_encode(value);
        const auto fc = nlm_candidate(f, code, mean);
        const auto next = ((1 - blend) * f) + (blend * fc);
        if (next != f) {
            value = image::srgb_decode(next);
            ++observation.changed;
        }
    }
    return {};
}
} // namespace
core::Result<NlmObservation> nlm_map(const NlmContext& context, NlmBand band,
                                     const NlmBandStorage& storage) {
    NlmObservation observation;
    for (std::uint32_t y = 0; y < band.rows; ++y) {
        if (auto mapped = map_row(context, band, storage, y, observation); !mapped) {
            return std::unexpected(std::move(mapped.error()));
        }
    }
    return observation;
}
} // namespace docenhance::methods
