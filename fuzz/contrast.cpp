// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/contrast.hpp"

#include "docenhance/image/numeric.hpp"
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
        auto result = methods::gamma_candidate(f, gamma);
        require(result.has_value() && std::abs(*result - std::pow(f, exponent)) < 1e-12,
                "gamma power reference");
        if (hi - lo >= methods::levels_minimum_range) {
            auto mapped = methods::levels_candidate(f, {.low = lo, .high = hi});
            require(mapped.has_value() &&
                        std::abs(*mapped - std::clamp((f - lo) / (hi - lo), 0.0, 1.0)) < 1e-12,
                    "levels scalar reference");
        }
    }
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    check({data, size});
    return 0;
}
