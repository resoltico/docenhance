// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/plane.hpp"

#include <cstdint>
namespace docenhance::methods {
struct Tvl1Vector {
    double x{};
    double y{};
};
inline Tvl1Vector tvl1_gradient(image::PlaneView<const double> u, std::uint32_t x,
                                std::uint32_t y) noexcept {
    const auto center = u.row(y).subspan(x, 1).front();
    return {
        .x = x + 1 < u.width() ? u.row(y).subspan(x + 1, 1).front() - center : 0,
        .y = y + 1 < u.height() ? u.row(y + 1).subspan(x, 1).front() - center : 0,
    };
}
inline double tvl1_adjoint(image::PlaneView<const double> px, image::PlaneView<const double> py,
                           std::uint32_t x, std::uint32_t y) noexcept {
    double value = 0;
    if (x > 0) {
        value += px.row(y).subspan(x - 1, 1).front();
    }
    if (y > 0) {
        value += py.row(y - 1).subspan(x, 1).front();
    }
    if (x + 1 < px.width()) {
        value -= px.row(y).subspan(x, 1).front();
    }
    if (y + 1 < py.height()) {
        value -= py.row(y).subspan(x, 1).front();
    }
    return value;
}
} // namespace docenhance::methods
