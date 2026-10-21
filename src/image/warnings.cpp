// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/warnings.hpp"

#include "docenhance/core/warnings.hpp"
#include "docenhance/image/output.hpp"

namespace docenhance::image {
core::WarningList warnings(const InterpretationReport& report) noexcept {
    core::WarningList result{};
    if (report.assumed_transfer || report.assumed_primaries) {
        std::get<0>(result) = core::Warning::profile_assumed;
    }
    if (report.interpretation == Interpretation::overridden_srgb) {
        std::get<1>(result) = core::Warning::profile_overridden;
    }
    if (report.flattened_pixels != 0) {
        std::get<2>(result) = core::Warning::alpha_flattened;
    }
    if (report.depth_reduced) {
        result.back() = core::Warning::depth_reduced;
    }
    return result;
}
} // namespace docenhance::image
