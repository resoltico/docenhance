// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>
namespace docenhance::tests {
struct TvReference {
    std::size_t width;
    std::size_t height;
    std::vector<double> input;
    std::vector<double> primal;
    std::vector<double> extrapolated;
    std::vector<double> px;
    std::vector<double> py;
    TvReference(std::size_t w, std::size_t h, std::vector<double> samples)
        : width(w), height(h), input(std::move(samples)), primal(input), extrapolated(input),
          px(input.size()), py(input.size()) {}
    [[nodiscard]] std::vector<double> scatter_adjoint() const {
        std::vector<double> result(input.size());
        for (std::size_t i = 0; i < result.size(); ++i) {
            if ((i % width) + 1 < width) {
                result[i] -= px[i];
                result[i + 1] += px[i];
            }
            if ((i / width) + 1 < height) {
                result[i] -= py[i];
                result[i + width] += py[i];
            }
        }
        return result;
    }
    [[nodiscard]] double objective(double lambda) const {
        double sum = 0;
        for (std::size_t i = 0; i < primal.size(); ++i) {
            const double dx = (i % width) + 1 < width ? primal[i + 1] - primal[i] : 0;
            const double dy = (i / width) + 1 < height ? primal[i + width] - primal[i] : 0;
            sum += std::sqrt((dx * dx) + (dy * dy)) + (lambda * std::abs(primal[i] - input[i]));
        }
        return sum;
    }
    void iterate(double lambda) {
        constexpr double step = 0.25;
        auto next_x = px;
        auto next_y = py;
        for (std::size_t i = 0; i < primal.size(); ++i) {
            const double dx = (i % width) + 1 < width ? extrapolated[i + 1] - extrapolated[i] : 0;
            const double dy =
                (i / width) + 1 < height ? extrapolated[i + width] - extrapolated[i] : 0;
            const double cx = px[i] + (step * dx);
            const double cy = py[i] + (step * dy);
            const double norm = std::max(1.0, std::sqrt((cx * cx) + (cy * cy)));
            next_x[i] = (i % width) + 1 < width ? cx / norm : 0;
            next_y[i] = (i / width) + 1 < height ? cy / norm : 0;
        }
        px = next_x;
        py = next_y;
        const auto adjoint = scatter_adjoint();
        auto next_u = primal;
        for (std::size_t i = 0; i < primal.size(); ++i) {
            const double v = primal[i] - (step * adjoint[i]);
            const double delta = v - input[i];
            const auto sign =
                static_cast<double>(static_cast<int>(delta > 0) - static_cast<int>(delta < 0));
            next_u[i] = std::clamp(
                input[i] + (sign * std::max(std::abs(delta) - (step * lambda), 0.0)), 0.0, 1.0);
            extrapolated[i] = next_u[i] + (next_u[i] - primal[i]);
        }
        primal = std::move(next_u);
    }
};
} // namespace docenhance::tests
