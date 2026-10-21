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
#include "docenhance/methods/parameter_domains.hpp"
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
constexpr std::uint32_t window_variants = 8;
constexpr std::uint32_t minimum_window = 3;
constexpr double byte_scale = 255.0;
docenhance::methods::Sauvola selected(std::span<const std::uint8_t> bytes) {
    namespace methods = docenhance::methods;
    const auto window =
        minimum_window + (2U * (docenhance::fuzz::binary_byte(bytes, 2) % window_variants));
    const auto k = docenhance::fuzz::binary_byte(bytes, 3) / byte_scale;
    const auto r = (1.0 + docenhance::fuzz::binary_byte(bytes, 4)) / (byte_scale + 1.0);
    return methods::Sauvola::create({
                                        .window = window,
                                        .k = k,
                                        .r = std::max(r, methods::sauvola_r_domain.minimum),
                                    })
        .value();
}
void check_output(docenhance::image::PlaneView<const std::uint16_t> source,
                  docenhance::image::PlaneView<const std::uint8_t> output,
                  const docenhance::methods::Sauvola& method,
                  const docenhance::methods::SauvolaFacts& facts) {
    std::uint64_t black = 0;
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            const auto expected = docenhance::tests::binary_sauvola_reference(source, method, x, y);
            docenhance::fuzz::require(output.row(y).subspan(x, 1).front() == expected,
                                      "actual Q16 rolling moments match direct integer windows");
            black += static_cast<std::uint64_t>(expected == 0);
        }
    }
    docenhance::fuzz::require(facts.black_samples == black &&
                                  facts.white_samples ==
                                      (std::uint64_t{source.width()} * source.height()) - black,
                              "actual Q16 Sauvola Facts count independent classifications");
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace fuzz = docenhance::fuzz;
    namespace image = docenhance::image;
    const std::span<const std::uint8_t> bytes{data, size};
    const image::Extent extent{
        .width = 1U + (fuzz::binary_byte(bytes, 0) % maximum_extent),
        .height = 1U + (fuzz::binary_byte(bytes, 1) % maximum_extent),
    };
    const auto method = selected(bytes);
    docenhance::core::Budget budget{budget_limit};
    auto source = fuzz::binary_codes(bytes, extent, 5, budget);
    auto output = image::Plane<std::uint8_t>::allocate(budget, extent.width, extent.height).value();
    const auto input = source.view().storage();
    const std::vector<std::uint16_t> before(input.begin(), input.end());
    const docenhance::exec::Scheduler scheduler{
        docenhance::exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto held = budget.used();
    const auto result = docenhance::methods::binarize(
        method, {source.view().as_const(), output.view(), budget, scheduler});
    fuzz::require(
        result && budget.used() == held && std::ranges::equal(input, before),
        "actual Q16 Sauvola succeeds with source preservation and complete scratch refund");
    check_output(source.view().as_const(), output.view().as_const(), method, *result);
    return 0;
}
