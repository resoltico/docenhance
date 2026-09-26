// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/surface.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>

namespace docenhance::methods {
namespace {
constexpr unsigned weight_row = 0;
constexpr unsigned right_row = 1; // Edge weight to the next cell in the same grid row.
constexpr unsigned down_row = 2;  // Edge weight to the same column in the next grid row.
constexpr unsigned solution_row = 3;
constexpr unsigned rhs_row = 4;
constexpr unsigned scratch_row = 5;
constexpr unsigned storage_rows = 6;
struct LevelView {
    std::uint32_t columns{};
    std::uint32_t rows{};
    std::span<double> weight;
    std::span<double> right;
    std::span<double> down;
    std::span<double> solution;
    std::span<double> rhs;
    std::span<double> scratch;
};
LevelView level_view(image::PlaneView<double> storage, const SurfaceLevel& level) {
    const auto size = std::size_t{level.columns} * level.rows;
    const auto part = [&](unsigned row) { return storage.row(row).subspan(level.offset, size); };
    return {
        .columns = level.columns,
        .rows = level.rows,
        .weight = part(weight_row),
        .right = part(right_row),
        .down = part(down_row),
        .solution = part(solution_row),
        .rhs = part(rhs_row),
        .scratch = part(scratch_row),
    };
}
std::size_t parent(const LevelView& fine, const LevelView& coarse, std::size_t i) noexcept {
    return (((i / fine.columns) / 2) * coarse.columns) + ((i % fine.columns) / 2);
}
double diagonal(const LevelView& level, double smooth, std::size_t i) noexcept {
    double edges = surface_at(level.right, i) + surface_at(level.down, i);
    if (i % level.columns != 0) {
        edges += surface_at(level.right, i - 1);
    }
    if (i >= level.columns) {
        edges += surface_at(level.down, i - level.columns);
    }
    return surface_at(level.weight, i) + (smooth * edges);
}
// scratch = A solution, with A = diag(weight) + smooth * weighted four-neighbor Laplacian.
core::Result<void> product(const LevelView& level, double smooth,
                           const core::Cancellation& cancellation) {
    std::ranges::fill(level.scratch, 0);
    for (std::size_t i = 0; i < level.solution.size(); ++i) {
        if (i % surface_poll_interval == 0 && cancellation.requested(core::Checkpoint::solving)) {
            return core::cancelled();
        }
        const double x = surface_at(level.solution, i);
        surface_at(level.scratch, i) += surface_at(level.weight, i) * x;
        if ((i % level.columns) + 1 < level.columns) {
            const double flow =
                smooth * surface_at(level.right, i) * (x - surface_at(level.solution, i + 1));
            surface_at(level.scratch, i) += flow;
            surface_at(level.scratch, i + 1) -= flow;
        }
        if (i + level.columns < level.solution.size()) {
            const double flow = smooth * surface_at(level.down, i) *
                                (x - surface_at(level.solution, i + level.columns));
            surface_at(level.scratch, i) += flow;
            surface_at(level.scratch, i + level.columns) -= flow;
        }
    }
    return {};
}
core::Result<void> relax(const LevelView& level, double smooth,
                         const core::Cancellation& cancellation) {
    for (unsigned sweep = 0; sweep < surface_smoothing_sweeps; ++sweep) {
        auto applied = product(level, smooth, cancellation);
        if (!applied) {
            return applied;
        }
        for (std::size_t i = 0; i < level.solution.size(); ++i) {
            surface_at(level.solution, i) +=
                surface_smoothing_damping *
                (surface_at(level.rhs, i) - surface_at(level.scratch, i)) /
                diagonal(level, smooth, i);
        }
    }
    return {};
}
void coarsen(const LevelView& fine, const LevelView& coarse) {
    std::ranges::fill(coarse.weight, 0);
    std::ranges::fill(coarse.right, 0);
    std::ranges::fill(coarse.down, 0);
    for (std::size_t i = 0; i < fine.weight.size(); ++i) {
        const auto target = parent(fine, coarse, i);
        surface_at(coarse.weight, target) += surface_at(fine.weight, i);
        // Only edges between different aggregates survive the Galerkin product.
        if ((i % fine.columns) % 2 == 1 && (i % fine.columns) + 1 < fine.columns) {
            surface_at(coarse.right, target) += surface_at(fine.right, i);
        }
        if ((i / fine.columns) % 2 == 1 && i + fine.columns < fine.weight.size()) {
            surface_at(coarse.down, target) += surface_at(fine.down, i);
        }
    }
}
} // namespace
core::Result<SurfaceHierarchy> SurfaceHierarchy::build(const SurfaceSystem& system,
                                                       core::Budget& budget) {
    std::array<SurfaceLevel, surface_level_limit> levels{};
    unsigned count = 0;
    std::size_t total = 0;
    SurfaceLevel level{.columns = system.grid.columns, .rows = system.grid.rows, .offset = 0};
    if (level.columns == 0 || level.rows == 0 ||
        std::size_t{level.columns} * level.rows != system.weights.size()) {
        return core::failure(core::ErrorCode::argument, "Invalid log-surface hierarchy grid");
    }
    while (true) {
        if (count == surface_level_limit) {
            return core::failure(core::ErrorCode::argument,
                                 "Log-surface grid needs too many levels");
        }
        level.offset = total;
        levels.at(count++) = level;
        total += std::size_t{level.columns} * level.rows;
        if (level.columns == 1 && level.rows == 1) {
            break;
        }
        level = {.columns = (level.columns + 1) / 2, .rows = (level.rows + 1) / 2, .offset = 0};
    }
    auto storage =
        image::Plane<double>::allocate(budget, static_cast<std::uint32_t>(total), storage_rows);
    if (!storage) {
        return std::unexpected(storage.error());
    }
    SurfaceHierarchy hierarchy{std::move(*storage), system.smooth};
    hierarchy.levels_ = levels;
    hierarchy.count_ = count;
    const auto view = hierarchy.storage_.view();
    const auto finest = level_view(view, levels.front());
    std::ranges::copy(system.weights, finest.weight.begin());
    for (std::size_t i = 0; i < finest.weight.size(); ++i) {
        surface_at(finest.right, i) = (i % finest.columns) + 1 < finest.columns ? 1 : 0;
        surface_at(finest.down, i) = i + finest.columns < finest.weight.size() ? 1 : 0;
    }
    for (unsigned l = 1; l < count; ++l) {
        coarsen(level_view(view, levels.at(l - 1)), level_view(view, levels.at(l)));
    }
    return hierarchy;
}
core::Result<void> SurfaceHierarchy::precondition(std::span<const double> residual,
                                                  std::span<double> output,
                                                  const core::Cancellation& cancellation) {
    const auto view = storage_.view();
    const auto level = [&](unsigned l) { return level_view(view, surface_at(levels(), l)); };
    const auto finest = level(0);
    if (residual.size() != finest.rhs.size() || output.size() != finest.solution.size()) {
        return core::failure(core::ErrorCode::argument, "Invalid log-surface V-cycle vectors");
    }
    std::ranges::copy(residual, finest.rhs.begin());
    for (unsigned l = 0; l + 1 < count_; ++l) {
        const auto fine = level(l);
        const auto coarse = level(l + 1);
        std::ranges::fill(fine.solution, 0);
        const auto relaxed = relax(fine, smooth_, cancellation);
        if (!relaxed) {
            return relaxed;
        }
        const auto applied = product(fine, smooth_, cancellation);
        if (!applied) {
            return applied;
        }
        std::ranges::fill(coarse.rhs, 0);
        for (std::size_t i = 0; i < fine.rhs.size(); ++i) {
            surface_at(coarse.rhs, parent(fine, coarse, i)) +=
                surface_at(fine.rhs, i) - surface_at(fine.scratch, i);
        }
    }
    const auto coarsest = level(count_ - 1);
    for (std::size_t i = 0; i < coarsest.solution.size(); ++i) {
        surface_at(coarsest.solution, i) =
            surface_at(coarsest.rhs, i) / diagonal(coarsest, smooth_, i);
    }
    for (unsigned l = count_ - 1; l > 0; --l) {
        const auto fine = level(l - 1);
        const auto coarse = level(l);
        for (std::size_t i = 0; i < fine.solution.size(); ++i) {
            surface_at(fine.solution, i) +=
                surface_coarse_scale * surface_at(coarse.solution, parent(fine, coarse, i));
        }
        const auto relaxed = relax(fine, smooth_, cancellation);
        if (!relaxed) {
            return relaxed;
        }
    }
    std::ranges::copy(finest.solution, output.begin());
    if (!std::ranges::all_of(output, [](double value) { return std::isfinite(value); })) {
        return core::failure(core::ErrorCode::numerical, "Nonfinite log-surface preconditioner");
    }
    return {};
}
} // namespace docenhance::methods
