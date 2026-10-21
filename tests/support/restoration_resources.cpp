// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "allocation_observer.hpp"
#include "docenhance/image/geometry.hpp"
#include "restoration_resource_calibration.hpp"
#include "restoration_resource_observation.hpp"
#include "restoration_resource_overlap.hpp"
#include "restoration_resource_pressure.hpp"

#include <array>
#include <cstdio>
#include <exception>

int main() {
    namespace tests = docenhance::tests;
    try {
        tests::restoration_resource_calibration();
        constexpr auto extents = std::to_array<docenhance::image::Extent>({
            {.width = 1, .height = 1},
            {.width = 1, .height = 409},
            {.width = 419, .height = 1},
            {.width = 129, .height = 97},
            {.width = 503, .height = 211},
        });
        for (const auto extent : extents) {
            tests::restoration_observed_case(extent);
            tests::restoration_pressure_cases(extent);
            tests::restoration_exact_budget(extent);
            tests::restoration_native_precedence(extent);
        }
        for (const auto callers : {1U, 2U, 7U}) {
            tests::restoration_overlap_case(callers);
        }
        // Independent PSF every-tap/inverse/DC/scale and internal-worker numerical/phase
        // controls retain their separately registered actual owners; these are whole calls.
        return tests::allocation_observer::observation_valid() ? 0 : 1;
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputc('\n', stderr));
        return 1;
    } catch (...) {
        static_cast<void>(std::fputs("Whole-method restoration resource failure\n", stderr));
        return 1;
    }
}
