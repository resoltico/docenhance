// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "nlm_detail.hpp"

#include <cstdint>
#include <expected>
#include <utility>
namespace docenhance::methods {
core::Result<NlmBandStorage> nlm_storage(const NlmContext& context, NlmBand band) {
    const auto width = context.prepared.get().codes.width();
    const auto patch = context.method.get().parameters().patch;
    auto accumulation = nlm_allocate<double>(context.working, width, 2 * band.rows);
    if (!accumulation) {
        return std::unexpected(std::move(accumulation.error()));
    }
    auto horizontal = nlm_allocate<std::uint32_t>(context.working, width, band.rows + patch - 1);
    if (!horizontal) {
        return std::unexpected(std::move(horizontal.error()));
    }
    auto differences = nlm_allocate<std::uint32_t>(context.working, width + patch - 1, 1);
    if (!differences) {
        return std::unexpected(std::move(differences.error()));
    }
    auto columns = nlm_allocate<std::uint32_t>(context.working, width, 1);
    if (!columns) {
        return std::unexpected(std::move(columns.error()));
    }
    const auto cancellation = context.working.scheduler.get().cancellation();
    for (std::uint32_t y = 0; y < accumulation->height(); ++y) {
        const auto row = accumulation->view().row(y);
        for (std::uint32_t x = 0; x < row.size(); ++x) {
            if (x % nlm_checkpoint_samples == 0 &&
                cancellation.requested(core::Checkpoint::initialization)) {
                return core::cancelled();
            }
            row.subspan(x, 1).front() = 0;
        }
    }
    return NlmBandStorage{
        .accumulation = std::move(*accumulation),
        .horizontal = std::move(*horizontal),
        .differences = std::move(*differences),
        .columns = std::move(*columns),
    };
}
} // namespace docenhance::methods
