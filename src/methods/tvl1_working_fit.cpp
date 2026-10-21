// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/tvl1.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "docenhance/methods/working.hpp"
#include "tvl1_detail.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>

namespace docenhance::methods {
namespace {
core::Result<image::Plane<double>> allocate(Working working) {
    if (working.scheduler.get().cancellation().requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    return image::Plane<double>::allocate(working.budget.get(), working.luminance.width(),
                                          working.luminance.height());
}
bool gather_row(Working working, Tvl1Field field, image::PlaneView<double> input,
                std::uint32_t y) noexcept {
    const auto source = working.luminance.row(y);
    const auto f = input.row(y);
    const auto u = field.primal.row(y);
    const auto bar = field.extrapolated.row(y);
    const auto px = field.dual_x.row(y);
    const auto py = field.dual_y.row(y);
    const auto cancellation = working.scheduler.get().cancellation();
    for (std::size_t x = 0; x < source.size(); ++x) {
        constexpr std::size_t interval = 1024;
        if (x % interval == 0 && cancellation.requested(core::Checkpoint::measurement)) {
            return false;
        }
        const auto value = image::srgb_encode(source.subspan(x, 1).front());
        f.subspan(x, 1).front() = value;
        u.subspan(x, 1).front() = value;
        bar.subspan(x, 1).front() = value;
        px.subspan(x, 1).front() = 0;
        py.subspan(x, 1).front() = 0;
    }
    return true;
}
core::Result<void> gather(Working working, Tvl1Field field, image::PlaneView<double> input) {
    const auto work = [&](std::size_t band) -> core::Result<void> {
        const auto [first, last] = tvl1_rows(input.height(), band);
        for (auto y = first; y < last; ++y) {
            if (!gather_row(working, field, input, y)) {
                return core::cancelled();
            }
        }
        return {};
    };
    return working.scheduler.get().for_each(tvl1_band_count(input.height()), exec::WorkRef{work});
}
core::Result<Tvl1Solution> completed(const Tvl1SolveState& progress) {
    if (!progress.stop || !progress.objective_start || !progress.objective_end) {
        return core::failure(core::ErrorCode::invariant, "TV-L1 solve has incomplete diagnostics");
    }
    return Tvl1Solution{
        .iterations = progress.iterations,
        .stop = *progress.stop,
        .passing_checkpoints = progress.passing_checkpoints,
        .primal_update = progress.primal_update,
        .dual_update = progress.dual_update,
        .objective_start = *progress.objective_start,
        .objective_end = *progress.objective_end,
    };
}
} // namespace
core::Result<Tvl1Fit> fit_tvl1(const Tvl1& method, Working working,
                               std::optional<Tvl1Stop>& observed_stop) {
    auto input = allocate(working);
    if (!input) {
        return std::unexpected(std::move(input.error()));
    }
    auto primal = allocate(working);
    if (!primal) {
        return std::unexpected(std::move(primal.error()));
    }
    auto bar = allocate(working);
    if (!bar) {
        return std::unexpected(std::move(bar.error()));
    }
    auto px = allocate(working);
    if (!px) {
        return std::unexpected(std::move(px.error()));
    }
    auto py = allocate(working);
    if (!py) {
        return std::unexpected(std::move(py.error()));
    }
    const Tvl1Field field{
        .input = input->view().as_const(),
        .primal = primal->view(),
        .extrapolated = bar->view(),
        .dual_x = px->view(),
        .dual_y = py->view(),
    };
    if (auto gathered = gather(working, field, input->view()); !gathered) {
        return std::unexpected(std::move(gathered.error()));
    }
    Tvl1SolveState progress;
    const Tvl1StopCapture capture{progress, observed_stop};
    if (auto solved = solve_tvl1(field, method, working.scheduler.get(), progress); !solved) {
        return std::unexpected(std::move(solved.error()));
    }
    auto solution = completed(progress);
    if (!solution) {
        return std::unexpected(std::move(solution.error()));
    }
    return Tvl1Fit{
        .input = std::move(*input),
        .primal = std::move(*primal),
        .solution = *solution,
    };
}
} // namespace docenhance::methods
