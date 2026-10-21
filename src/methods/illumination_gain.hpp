// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/illumination.hpp"

#include <algorithm>

namespace docenhance::methods {
struct IlluminationGain {
    double target{};
    double strength{};
    double max_gain{};
};
struct IlluminationAdjustment {
    double target{};
    double gain{};
    bool capped{};
    bool saturated{};
};
// Producer-admitted finite Unit Y/background/target and factory-admitted strength/gain.
[[nodiscard]] IlluminationAdjustment illumination_adjustment(double y, double background,
                                                             IlluminationGain gain) noexcept;
// Finite unit background/target; the floor keeps this ratio finite even for black fields.
inline double illumination_gain_ratio(double background, double target) noexcept {
    return target / std::max(background, illumination_floor);
}
} // namespace docenhance::methods
