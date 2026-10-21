// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/contrast.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/methods/parameters.hpp"

#include <algorithm>
#include <cmath>
namespace docenhance::methods {
core::Result<Levels> Levels::create(LevelsParameters p) {
    if (!valid_parameters(p) || p.high <= p.low) {
        return core::failure(core::ErrorCode::argument, "Invalid levels percentiles or blend");
    }
    return Levels{p};
}
core::Result<Clahe> Clahe::create(ClaheParameters p) {
    if (!valid_parameters(p)) {
        return core::failure(core::ErrorCode::argument, "Invalid CLAHE grid, clip or blend");
    }
    return Clahe{p};
}
core::Result<Gamma> Gamma::create(GammaParameters p) {
    if (!valid_parameters(p)) {
        return core::failure(core::ErrorCode::argument, "Invalid gamma exponent or blend");
    }
    return Gamma{p};
}
double levels_candidate(double f, LevelsRange range) noexcept {
    return std::clamp((f - range.low) / (range.high - range.low), 0.0, 1.0);
}
double gamma_candidate(double f, const Gamma& method) noexcept {
    if (f == 0 || f == 1 || method.parameters().gamma == 1) {
        return f;
    }
    return std::pow(f, method.parameters().gamma);
}
} // namespace docenhance::methods
