// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/binarization.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/parameters.hpp"

#include <variant>

namespace docenhance::methods {
core::Result<FixedThreshold> FixedThreshold::create(double threshold) {
    if (!valid_parameters(FixedThresholdParameters{.threshold = threshold})) {
        return core::failure(core::ErrorCode::argument, "B03 requires a finite threshold in [0,1]");
    }
    return FixedThreshold{threshold};
}
core::Result<Sauvola> Sauvola::create(SauvolaParameters parameters) {
    if (!valid_parameters(parameters)) {
        return core::failure(core::ErrorCode::argument, "Invalid B02 window, k or normalized R");
    }
    return Sauvola{parameters};
}
ImplementedMethod describe(const Binarization& method) {
    return std::visit([](const auto& parameters) { return parameters.descriptor(); }, method);
}
} // namespace docenhance::methods
