// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "binary_reference.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "support/binary_codes.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace {
constexpr std::size_t budget_limit = 65536;
constexpr std::uint32_t maximum_extent = 13;
void check_output(docenhance::image::PlaneView<const std::uint16_t> source,
                  docenhance::image::PlaneView<const std::uint8_t> output,
                  const docenhance::methods::OtsuFacts& facts) {
    using docenhance::fuzz::require;
    std::uint64_t black = 0;
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            const auto bin =
                docenhance::tests::binary_reference_bin(source.row(y).subspan(x, 1).front());
            const bool expected = bin <= facts.threshold_bin;
            black += static_cast<std::uint64_t>(expected);
            require(output.row(y).subspan(x, 1).front() == (expected ? 0 : 1),
                    "actual Q16 Otsu uses independent bin and direct0/1 polarity");
        }
    }
    require(facts.black_samples == black &&
                facts.white_samples == (std::uint64_t{source.width()} * source.height()) - black,
            "actual Q16 Otsu complete counters match independent decisions");
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using docenhance::fuzz::require;
    namespace fuzz = docenhance::fuzz;
    namespace image = docenhance::image;
    const std::span<const std::uint8_t> bytes{data, size};
    const image::Extent extent{
        .width = 1U + (fuzz::binary_byte(bytes, 0) % maximum_extent),
        .height = 1U + (fuzz::binary_byte(bytes, 1) % maximum_extent),
    };
    docenhance::core::Budget budget{budget_limit};
    auto source = fuzz::binary_codes(bytes, extent, 2, budget);
    auto output = image::Plane<std::uint8_t>::allocate(budget, extent.width, extent.height).value();
    const auto input = source.view().storage();
    const std::vector<std::uint16_t> before(input.begin(), input.end());
    const auto expected = docenhance::tests::binary_otsu_reference(source.view().as_const());
    const docenhance::exec::Scheduler scheduler{
        docenhance::exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto held = budget.used();
    const auto result =
        docenhance::methods::binarize(docenhance::methods::Otsu::create(),
                                      {source.view().as_const(), output.view(), budget, scheduler});
    require(result && result->threshold_bin == expected.threshold &&
                result->single_bin_fallback == expected.fallback && budget.used() == held &&
                std::ranges::equal(input, before),
            "actual Q16 histogram fit matches independent merged-bin populations and refunds");
    check_output(source.view().as_const(), output.view().as_const(), *result);
    return 0;
}
