// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/surface.hpp"
#include "surface_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace docenhance::methods {
core::Result<void> apply_surface_system(const SurfaceSystem& system, std::span<const double> vector,
                                        std::span<double> output,
                                        const core::Cancellation& cancellation) {
    const auto columns = system.grid.columns;
    const auto rows = system.grid.rows;
    const auto size = std::size_t{columns} * rows;
    if (columns == 0 || rows == 0 || size > surface_cell_limit || vector.size() != size ||
        output.size() != size || system.weights.size() != size || !std::isfinite(system.smooth) ||
        system.smooth <= 0 || !std::ranges::all_of(system.weights, [](double w) {
            return std::isfinite(w) && w >= 0 && w <= 1;
        })) {
        return core::failure(core::ErrorCode::argument, "Invalid log-surface system");
    }
    for (std::size_t i = 0; i < size; ++i) {
        if (i % surface_poll_interval == 0 && cancellation.requested(core::Checkpoint::solving)) {
            return core::cancelled();
        }
        const double center = surface_at(vector, i);
        double difference = 0;
        if (i % columns != 0) {
            difference += center - surface_at(vector, i - 1);
        }
        if ((i % columns) + 1 < columns) {
            difference += center - surface_at(vector, i + 1);
        }
        if (i >= columns) {
            difference += center - surface_at(vector, i - columns);
        }
        if (i + columns < size) {
            difference += center - surface_at(vector, i + columns);
        }
        surface_at(output, i) =
            (surface_at(system.weights, i) * center) + (system.smooth * difference);
    }
    return {};
}
} // namespace docenhance::methods
