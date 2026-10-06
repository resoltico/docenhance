// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/tvl1.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"
#include "tvl1_reference.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace {
constexpr std::size_t budget_limit = std::size_t{2} * 1024 * 1024;
constexpr unsigned dimension_choices = 8;
constexpr unsigned iterations = 30;
constexpr double fidelity = 1.5;
constexpr double tolerance = 1e-12;
constexpr double sample_maximum = 65535;
std::uint8_t byte(std::span<const std::uint8_t> input, std::size_t i) {
    return i < input.size() ? input.subspan(i, 1).front() : 0;
}
void compare(std::span<const std::uint8_t> bytes) {
    namespace image = docenhance::image;
    namespace methods = docenhance::methods;
    using docenhance::fuzz::require;
    const image::Extent extent{
        .width = 1U + (byte(bytes, 0) % dimension_choices),
        .height = 1U + (byte(bytes, 1) % dimension_choices),
    };
    docenhance::core::Budget budget{budget_limit};
    auto input = image::Plane<double>::allocate(budget, extent.width, extent.height).value();
    auto primal = image::Plane<double>::allocate(budget, extent.width, extent.height).value();
    auto bar = image::Plane<double>::allocate(budget, extent.width, extent.height).value();
    auto px = image::Plane<double>::allocate(budget, extent.width, extent.height).value();
    auto py = image::Plane<double>::allocate(budget, extent.width, extent.height).value();
    std::vector<double> values;
    for (unsigned y = 0; y < extent.height; ++y) {
        for (unsigned x = 0; x < extent.width; ++x) {
            const auto i = (std::size_t{y} * extent.width) + x;
            const auto word = (unsigned{byte(bytes, 2 + (2 * i))} * 256) + byte(bytes, 3 + (2 * i));
            const double value = word / sample_maximum;
            values.push_back(value);
            input.view().row(y).subspan(x, 1).front() = value;
            primal.view().row(y).subspan(x, 1).front() = value;
            bar.view().row(y).subspan(x, 1).front() = value;
            px.view().row(y).subspan(x, 1).front() = 0;
            py.view().row(y).subspan(x, 1).front() = 0;
        }
    }
    docenhance::tests::TvReference reference{extent.width, extent.height, values};
    const methods::Tvl1Field field{
        .input = input.view().as_const(),
        .primal = primal.view(),
        .extrapolated = bar.view(),
        .dual_x = px.view(),
        .dual_y = py.view(),
    };
    for (unsigned iteration = 0; iteration < iterations; ++iteration) {
        require(methods::tvl1_iteration(field, fidelity).has_value(), "TV-L1 finite iterates");
        reference.iterate(fidelity);
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto value = primal.view()
                               .row(static_cast<unsigned>(i / extent.width))
                               .subspan(i % extent.width, 1)
                               .front();
        require(std::isfinite(value) && value >= 0 && value <= 1, "TV-L1 finite bounded result");
        require(std::abs(value - reference.primal[i]) < tolerance,
                "TV-L1 simultaneous scattered-adjoint reference");
    }
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    compare({data, size});
    return 0;
}
