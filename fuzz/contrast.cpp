// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/contrast.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/clahe.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/working.hpp"
#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace {
void contextual(docenhance::fuzz::FuzzInput& input) {
    namespace methods = docenhance::methods;
    namespace image = docenhance::image;
    namespace exec = docenhance::exec;
    using docenhance::fuzz::require;
    constexpr std::size_t budget_limit = std::size_t{1024} * 1024;
    constexpr std::uint32_t side = 32;
    docenhance::core::Budget budget{budget_limit};
    {
        const double clip = 1 + (static_cast<double>(input.byte()) / 255 * 7);
        auto source = image::Plane<double>::allocate(budget, side, side);
        auto repeated = image::Plane<double>::allocate(budget, side, side);
        const auto selected =
            methods::Clahe::create({.grid_columns = 2, .grid_rows = 2, .clip = clip});
        const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
        require(source && repeated && selected && concurrency, "CLAHE bounded eager inputs");
        if (!source || !repeated || !selected || !concurrency) {
            return;
        }
        for (std::uint32_t y = 0; y < side; ++y) {
            const auto first = source->view().row(y);
            const auto second = repeated->view().row(y);
            for (std::uint32_t x = 0; x < side; ++x) {
                const auto value = static_cast<double>(input.integer<std::uint16_t>()) / 65535;
                first.subspan(x, 1).front() = value;
                second.subspan(x, 1).front() = value;
            }
        }
        const auto resident = budget.used();
        const exec::Scheduler scheduler{*concurrency};
        const auto facts = methods::apply(*selected, {source->view(), {}, budget, scheduler});
        require(facts && methods::validate(*facts, selected->parameters()),
                "CLAHE completed Facts");
        if (!facts) {
            return;
        }
        const auto frozen = *facts;
        const auto again = methods::apply(*selected, {repeated->view(), {}, budget, scheduler});
        require(again && *facts == frozen && *again == frozen && budget.used() == resident,
                "CLAHE repeats deterministically without mutating completed Facts or retaining "
                "scratch");
        for (std::uint32_t y = 0; y < side; ++y) {
            const auto row = source->view().row(y);
            require(std::ranges::equal(row, repeated->view().row(y)),
                    "CLAHE repeated samples exact");
            require(std::ranges::all_of(row,
                                        [](double value) {
                                            return std::isfinite(value) && value >= 0 && value <= 1;
                                        }),
                    "CLAHE finite unit luminance");
        }
    }
    require(budget.used() == 0, "CLAHE eager owners refund every charge");
}
void check(std::span<const std::uint8_t> bytes) {
    docenhance::fuzz::FuzzInput input{bytes};
    using docenhance::fuzz::require;
    namespace image = docenhance::image;
    namespace methods = docenhance::methods;
    constexpr unsigned choices = 64;
    const auto count = 1U + (input.byte() % choices);
    const double exponent = .25 + (static_cast<double>(input.byte()) / 255 * 3.75);
    const auto gamma = methods::Gamma::create({.gamma = exponent}).value();
    std::vector<double> values;
    values.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        values.push_back(static_cast<double>(input.integer<std::uint16_t>()) / 65535);
    }
    auto expected = values;
    std::ranges::sort(expected);
    auto sorted = values;
    require(image::sort_samples(sorted).has_value(), "bounded finite percentile sort");
    require(sorted == expected, "independent quantile ordering");
    for (const double p : {0.0, .005, .5, .995, 1.0}) {
        const auto rank = static_cast<std::size_t>(std::ceil(p * count));
        auto index = image::nearest_rank_index(count, p);
        require(index.has_value() && *index == (rank == 0 ? 0 : rank - 1),
                "independent nearest ranks");
    }
    const auto lo = expected.front();
    const auto hi = expected.back();
    for (const auto f : values) {
        const double result = methods::gamma_candidate(f, gamma);
        require(std::isfinite(result) && result >= 0 && result <= 1,
                "gamma produces finite unit output before approximate reference use");
        require(std::abs(result - std::pow(f, exponent)) < 1e-12, "gamma power reference");
        if (hi - lo >= methods::levels_minimum_range) {
            const double mapped = methods::levels_candidate(f, {.low = lo, .high = hi});
            require(std::isfinite(mapped) && mapped >= 0 && mapped <= 1,
                    "Levels produces finite unit output before approximate reference use");
            require(std::abs(mapped - std::clamp((f - lo) / (hi - lo), 0.0, 1.0)) < 1e-12,
                    "levels scalar reference");
        }
    }
    docenhance::fuzz::FuzzInput contextual_input{bytes};
    contextual(contextual_input);
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    check({data, size});
    return 0;
}
