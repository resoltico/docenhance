// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/sharpening.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/methods/unsharp.hpp"
#include "sharpen_reference.hpp"
#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace {
void scalar_check(docenhance::fuzz::FuzzInput& input, const docenhance::methods::Unsharp& method) {
    const double f = static_cast<double>(input.byte()) / 255;
    const double b = static_cast<double>(input.byte()) / 255;
    const double d = f - b;
    const auto p = method.parameters();
    const double residual =
        std::abs(d) <= p.threshold / 255 ? 0 : d - (d > 0 ? p.threshold / 255 : -p.threshold / 255);
    const auto candidate = docenhance::methods::unsharp_candidate(f, b, method);
    docenhance::fuzz::require(std::isfinite(candidate),
                              "independent finite raw unsharp output before piecewise oracle");
    docenhance::fuzz::require(std::abs(candidate - (f + (p.amount * residual))) < 1e-14,
                              "independent piecewise soft threshold");
}
void fill_pixels(docenhance::fuzz::FuzzInput& input, docenhance::image::PlaneView<double> source,
                 docenhance::image::PlaneView<double> repeated, std::vector<double>& linear,
                 std::vector<double>& perceptual) {
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            const auto value = static_cast<double>(input.integer<std::uint16_t>()) / 65535;
            source.row(y).subspan(x, 1).front() = value;
            repeated.row(y).subspan(x, 1).front() = value;
            linear.push_back(value);
            perceptual.push_back(docenhance::tests::sharpen_reference_srgb_encode(value));
        }
    }
}
void compare_pixels(docenhance::image::PlaneView<const double> source,
                    docenhance::image::PlaneView<const double> repeated,
                    const std::vector<double>& linear, const std::vector<double>& perceptual,
                    const std::vector<double>& expected) {
    using docenhance::fuzz::require;
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        require(std::ranges::equal(source.row(y), repeated.row(y)),
                "exact repeated sharpening samples");
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            const auto index = (std::size_t{y} * source.width()) + x;
            const auto target = docenhance::tests::sharpen_reference_linear_output(
                linear.at(index), perceptual.at(index), expected.at(index));
            const auto actual = source.row(y).subspan(x, 1).front();
            require(std::isfinite(actual) && actual >= 0 && actual <= 1,
                    "finite unit sharpened luminance before approximate comparison");
            require(std::abs(actual - target) < 2e-12,
                    "independent direct2D Gaussian, soft threshold and exact no-op reconstruction");
        }
    }
}
void check(docenhance::fuzz::FuzzInput& input, docenhance::core::Budget& budget) {
    namespace methods = docenhance::methods;
    namespace image = docenhance::image;
    namespace exec = docenhance::exec;
    using docenhance::fuzz::require;
    constexpr std::uint32_t width = 9;
    constexpr std::uint32_t height = 7;
    const double amount = static_cast<double>(input.byte()) / 255 * 2;
    const double threshold = static_cast<double>(input.byte()) / 255 * 20;
    const double sigma = 0.3 + (static_cast<double>(input.byte()) / 255 * 2.7);
    const auto method =
        methods::Unsharp::create({.sigma = sigma, .amount = amount, .threshold = threshold});
    auto source = image::Plane<double>::allocate(budget, width, height);
    auto repeated = image::Plane<double>::allocate(budget, width, height);
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    require(method && source && repeated && concurrency, "bounded eager sharpening inputs");
    if (!method || !source || !repeated || !concurrency) {
        return;
    }
    scalar_check(input, *method);
    std::vector<double> linear;
    std::vector<double> perceptual;
    fill_pixels(input, source->view(), repeated->view(), linear, perceptual);
    const auto expected =
        docenhance::tests::sharpen_reference(perceptual, {
                                                             .width = width,
                                                             .height = height,
                                                             .sigma = sigma,
                                                             .amount = amount,
                                                             .threshold = threshold,
                                                         });
    const auto resident = budget.used();
    const exec::Scheduler scheduler{*concurrency};
    const auto facts = methods::apply(*method, {source->view(), {}, budget, scheduler});
    require(facts && methods::validate(*facts, method->parameters()),
            "actual complete sharpening Facts");
    if (!facts) {
        return;
    }
    const auto frozen = *facts;
    const auto again = methods::apply(*method, {repeated->view(), {}, budget, scheduler});
    require(again && *facts == frozen && *again == frozen && budget.used() == resident,
            "sharpening repeats exactly, retains completed Facts and refunds all scratch");
    compare_pixels(source->view().as_const(), repeated->view().as_const(), linear, perceptual,
                   expected);
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    docenhance::fuzz::FuzzInput input{{data, size}};
    constexpr std::size_t limit = std::size_t{1024} * 1024;
    docenhance::core::Budget budget{limit};
    check(input, budget);
    docenhance::fuzz::require(budget.used() == 0, "eager sharpening refunds every owner");
    return 0;
}
