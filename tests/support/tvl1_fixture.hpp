// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/tvl1.hpp"

#include <cstddef>
#include <vector>

namespace docenhance::tests {
struct TvPlanes {
    image::Plane<double> f;
    image::Plane<double> u;
    image::Plane<double> bar;
    image::Plane<double> px;
    image::Plane<double> py;
    TvPlanes(core::Budget& budget, image::Extent extent)
        : f(image::Plane<double>::allocate(budget, extent.width, extent.height).value()),
          u(image::Plane<double>::allocate(budget, extent.width, extent.height).value()),
          bar(image::Plane<double>::allocate(budget, extent.width, extent.height).value()),
          px(image::Plane<double>::allocate(budget, extent.width, extent.height).value()),
          py(image::Plane<double>::allocate(budget, extent.width, extent.height).value()) {}
    methods::Tvl1Field field() {
        return {
            .input = f.view().as_const(),
            .primal = u.view(),
            .extrapolated = bar.view(),
            .dual_x = px.view(),
            .dual_y = py.view(),
        };
    }
    void fill(const std::vector<double>& values) {
        for (unsigned y = 0; y < f.height(); ++y) {
            for (unsigned x = 0; x < f.width(); ++x) {
                const auto value = values[(std::size_t{y} * f.width()) + x];
                f.view().row(y).subspan(x, 1).front() = value;
                u.view().row(y).subspan(x, 1).front() = value;
                bar.view().row(y).subspan(x, 1).front() = value;
                px.view().row(y).subspan(x, 1).front() = 0;
                py.view().row(y).subspan(x, 1).front() = 0;
            }
        }
    }
};
} // namespace docenhance::tests
