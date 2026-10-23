// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_working.hpp"
#include "restoration_working_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iomanip>
#include <iostream>
#include <span>
#include <string_view>
namespace {
bool verify_r5() {
    namespace core = docenhance::core;
    namespace exec = docenhance::exec;
    namespace image = docenhance::image;
    namespace methods = docenhance::methods;
    constexpr std::uint32_t width = 1000000;
    constexpr std::uint32_t height = 7;
    constexpr std::size_t budget_bytes = std::size_t{4096} * 1024 * 1024;
    constexpr double tolerance = 1e-12;
    core::Budget budget{budget_bytes};
    auto luminance = image::Plane<double>::allocate(budget, width, height).value();
    auto mask = image::Plane<std::uint8_t>::allocate(budget, width, height).value();
    const auto value = image::srgb_decode(235.0 / 255.0);
    std::ranges::fill(mask.view().storage(), 0);
    for (std::uint32_t y = 0; y < height; ++y) {
        std::ranges::fill(luminance.view().row(y), value);
    }
    const auto held = budget.used();
    const auto method = methods::Wiener::create({.k = 1, .blend = 0.5}).value();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(7, 7, 0, 0).value()};
    const auto result =
        methods::apply(method, docenhance::tests::restoration_gaussian(),
                       {luminance.view(), mask.view().as_const(), budget, scheduler});
    if (!result || !result->transform || result->transform->width != 1012500 ||
        result->transform->height != 72 ||
        result->context_samples != std::uint64_t{width} * height ||
        result->counts.evaluated != std::uint64_t{width} * height || budget.used() != held) {
        std::cerr << "Actual R5 application/geometry/context/refund failure\n";
        return false;
    }
    double maximum_error = 0;
    const auto output = luminance.view();
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto row = output.row(y);
        for (const auto actual : row) {
            if (!std::isfinite(actual)) {
                std::cerr << "R5 output contains a nonfinite sample\n";
                return false;
            }
            const auto error = std::abs(actual - value);
            if (!std::isfinite(error)) {
                std::cerr << "R5 output error is nonfinite\n";
                return false;
            }
            maximum_error = std::max(maximum_error, error);
        }
    }
    std::cout << std::setprecision(17) << R"({"case":"R5","width":)" << width
              << ",\"height\":" << height << R"(,"gray":235,"linear_y":)" << value
              << ",\"padded_mean\":" << result->transform->padded_mean
              << ",\"maximum_absolute_error\":" << maximum_error << ",\"tolerance\":" << tolerance
              << ",\"budget_bytes\":" << budget_bytes << ",\"charged_peak\":" << budget.peak()
              << "}\n";
    return maximum_error <= tolerance;
}
} // namespace
int main(int argc, char* const* const argv) {
    if (argc != 2 ||
        std::string_view{std::span{argv, static_cast<std::size_t>(argc)}.subspan(1, 1).front()} !=
            "--r5-full") {
        static_cast<void>(
            std::fputs("An explicitly authorized --r5-full invocation is required\n", stderr));
        return 2;
    }
    try {
        return verify_r5() ? 0 : 1;
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs("R5 numerical verification exception: ", stderr));
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputc('\n', stderr));
        return 1;
    } catch (...) {
        static_cast<void>(std::fputs("R5 numerical verification unknown exception\n", stderr));
        return 1;
    }
}
