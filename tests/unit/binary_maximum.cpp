// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "binary_fixture.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "require.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <iostream>
namespace docenhance::tests {
namespace {
void maximum_population_backing(const BinaryFixture& fixture) {
    constexpr std::uint32_t side = 4095;
    constexpr std::uint32_t center = 2047;
    constexpr std::uint16_t high = 65535;
    constexpr std::uint16_t low = 65534;
    const auto source = fixture.codes.view();
    const auto output = fixture.out.view();
    bool exact = true;
    for (std::uint32_t y = 0; y < side; ++y) {
        const auto in = source.row(y);
        const auto out = output.row(y);
        for (std::uint32_t x = 0; x < side; ++x) {
            const bool exceptional = x == center && y == center;
            exact = exact && in.subspan(x, 1).front() == (exceptional ? low : high) &&
                    out.subspan(x, 1).front() == (exceptional ? 0 : 1);
        }
    }
    require(exact, "entire maximum-population input and actual binary output independently agree");
    for (std::size_t i = 0; i < source.storage().size(); ++i) {
        if (i % source.row_pitch() >= side) {
            require(source.storage().subspan(i, 1).front() == binary_padding,
                    "maximum-population source padding remains exact");
        }
    }
    for (std::size_t i = 0; i < output.storage().size(); ++i) {
        if (i % output.row_pitch() >= side) {
            require(output.storage().subspan(i, 1).front() == binary_sentinel,
                    "maximum-population destination padding remains exact");
        }
    }
}
void maximum_population_case() {
    constexpr std::uint32_t side = 4095;
    constexpr std::uint32_t center = 2047;
    constexpr std::uint16_t high = 65535;
    constexpr std::uint16_t low = 65534;
    constexpr std::size_t local_budget = std::size_t{64} * 1024 * 1024;
    BinaryFixture fixture{side, side, local_budget};
    for (std::uint32_t y = 0; y < side; ++y) {
        std::ranges::fill(fixture.codes.view().row(y), high);
    }
    fixture.codes.view().row(center).subspan(center, 1).front() = low;
    const auto resident = fixture.budget.used();
    const auto method =
        methods::Sauvola::create({.window = side, .k = 0x1.0000ff10e1200p-16, .r = 0.5}).value();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto result = methods::binarize(
        method, {fixture.codes.view().as_const(), fixture.out.view(), fixture.budget, scheduler});
    require(result && result->black_samples == 1 &&
                result->white_samples == (std::uint64_t{side} * side) - 1 &&
                fixture.out.view().row(center).subspan(center, 1).front() == 0 &&
                fixture.budget.used() == resident,
            "real N-member window distinguishes exact variance from rounded-product wrong-white "
            "result");
    maximum_population_backing(fixture);
    require(fixture.budget.peak() == 50385024,
            "actual Q/output/moment/count high-water agrees with the independent full reservation");
    std::cout << "Binary maximum witness expected N=16769025 S=1098958053374 P=72020216027799556 "
                 "V=16769024"
              << " expected_threshold=0x1.fffc000000200p+15 black=" << result->black_samples
              << " white=" << result->white_samples << " peak=" << fixture.budget.peak()
              << " refunded=" << (fixture.budget.used() == resident) << "\n";
}
} // namespace
TEST_CASE("Binary Q16 Sauvola executes the real4095 squared discriminating population") {
    const auto before = assertion_counter().load();
    maximum_population_case();
    REQUIRE(assertion_counter().load() > before);
}
} // namespace docenhance::tests
