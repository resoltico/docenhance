// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/reviewed_methods.hpp"
namespace docenhance::methods {
// Admitted G02 configuration; image supplies the exact coordinate permutation.
struct ExactQuarterTurn {
    image::QuarterTurn rotation = image::QuarterTurn::identity();
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return quarter_turn_descriptor;
    }
};
} // namespace docenhance::methods
