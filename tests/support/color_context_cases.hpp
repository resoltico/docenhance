// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "allocation_observer.hpp"
#include "context.hpp"
#include "docenhance/color/profile.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "fft_allocation_trace.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <span>

namespace docenhance::tests {
namespace color_context_detail {
struct Observation {
    std::size_t c_attempts{};
    std::size_t retained{};
    bool resource = false;
    bool unavailable = false;
};
inline Observation observe(std::span<const std::byte> profile, bool validate,
                           bool fail_bootstrap = false) {
    namespace allocation = allocation_observer;
    core::Budget budget{fail_bootstrap ? std::size_t{16} * 1024 * 1024 : 0};
    allocation::allocation_attempts.store(0);
    allocation::c_allocation_attempts.store(0);
    allocation::peak.store(0);
    allocation::failure_at.store(fail_bootstrap ? 1 : 0);
    fft_allocation_trace::reset();
    Observation observation;
    allocation::observing.store(true);
    {
        if (validate) {
            const auto result = color::validate_output_profile(
                {.width = 1, .height = 1, .depth = image::PngDepth()}, profile, budget, {});
            observation.resource = !result && result.error().code == core::ErrorCode::resource;
        } else {
            const color::Context context{budget};
            observation.unavailable = !context.good();
            observation.resource = context.error("Context entry").code == core::ErrorCode::resource;
        }
    }
    allocation::observing.store(false);
    allocation::failure_at.store(0);
    observation.c_attempts = allocation::c_allocation_attempts.load();
    observation.retained = allocation::live.load();
    const char* unavailable = observation.unavailable ? "true" : "false";
    if (validate) {
        unavailable = "null";
    }
    std::cout << R"({"color_context_validate":)" << validate << R"(,"failed_bootstrap":)"
              << fail_bootstrap << R"(,"resource":)" << observation.resource << R"(,"unavailable":)"
              << unavailable << R"(,"c_attempts":)" << observation.c_attempts << R"(,"retained":)"
              << observation.retained << R"(,"budget_used":)" << budget.used() << R"(,"events":)";
    fft_allocation_trace::write(std::cout);
    std::cout << "}\n";
    if (budget.used() != 0) {
        observation.resource = false;
    }
    return observation;
}
} // namespace color_context_detail
inline bool color_context_cases() {
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    auto gray = color::make_output_profile(budget, true);
    auto rgb = color::make_output_profile(budget, false);
    if (!gray || !rgb ||
        !color::validate_output_profile({.width = 1, .height = 1, .depth = image::PngDepth()},
                                        gray->bytes(), budget, {}) ||
        !color::validate_output_profile(
            {.width = 1, .height = 1, .model = image::SampleModel::rgb, .depth = image::PngDepth()},
            rgb->bytes(), budget, {})) {
        return false;
    }
    const auto binary_depth = image::PngDepth::from_bits(1);
    if (!binary_depth) {
        return false;
    }
    const auto binary_profile = color::validate_output_profile(
        {.width = 1, .height = 1, .depth = *binary_depth}, gray->bytes(), budget, {});
    if (binary_profile || binary_profile.error().code != core::ErrorCode::input) {
        return false;
    }
    // Warm the same failed-context path before observing. Keep legitimate fixed-bootstrap
    // allocations/free events; no zero-native-event claim or filtered provider lane exists.
    {
        core::Budget empty{0};
        const color::Context context{empty};
        if (context.good()) {
            return false;
        }
    }
    const auto baseline = color_context_detail::observe(gray->bytes(), false);
    const auto repeated = color_context_detail::observe(gray->bytes(), false);
    const auto validation = color_context_detail::observe(gray->bytes(), true);
    const auto unavailable = color_context_detail::observe(gray->bytes(), false, true);
    if (!baseline.resource || !baseline.unavailable || baseline.c_attempts == 0 ||
        baseline.retained != 0 || !repeated.resource ||
        repeated.c_attempts != baseline.c_attempts || repeated.retained != 0 ||
        !validation.resource || validation.c_attempts != baseline.c_attempts ||
        validation.retained != 0 || !unavailable.resource || !unavailable.unavailable ||
        unavailable.c_attempts != 1 || unavailable.retained != 0) {
        return false;
    }
    std::array<std::byte, 128> malformed{};
    malformed.at(3) = std::byte{128};
    const auto bad_profile = color::validate_output_profile(
        {.width = 1, .height = 1, .depth = image::PngDepth()}, malformed, budget, {});
    if (bad_profile || bad_profile.error().code != core::ErrorCode::input) {
        return false;
    }
    core::Budget empty{0};
    malformed.at(3) = std::byte{0};
    const auto bad_size = color::validate_output_profile(
        {.width = 1, .height = 1, .depth = image::PngDepth()}, malformed, empty, {});
    return !bad_size && bad_size.error().code == core::ErrorCode::input && empty.used() == 0;
}
} // namespace docenhance::tests
