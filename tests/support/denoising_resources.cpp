// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "allocation_observer.hpp"
#include "nlm_launch_gate.hpp"
#include "nlm_resource_cases.hpp"
#include "nlm_resource_extremes.hpp"
#include "nlm_resource_observation.hpp"
#include "nlm_resource_overlap.hpp"
#include "nlm_resource_pressure.hpp"

#include <cstdio>
#include <exception>

int main() {
    namespace tests = docenhance::tests;
    try {
        if (!tests::allocation_observer::initialize_hooks()) {
            return 1;
        }
        tests::nlm_warm_exception_runtime();
        tests::nlm_launch_gate_controls();
        tests::nlm_observer_calibration();
        tests::nlm_resource_healthy();
        tests::nlm_resource_pressure();
        const bool serial_attained = tests::nlm_observed_failures(1);
        const bool parallel_attained = tests::nlm_observed_failures(2);
        tests::nlm_resource_overlap();
        tests::nlm_resource_extremes();
        // Full 9x449 independent output/Facts and 1/2/7-worker determinism remain in
        // nlm_working_control_cases; sparse extreme checks make no full-count oracle claim.
        return serial_attained && parallel_attained ? 0 : 1;
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputc('\n', stderr));
        return 1;
    } catch (...) {
        static_cast<void>(std::fputs("NLM resource observation exception\n", stderr));
        return 1;
    }
}
