// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "binary_fixture.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "require.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>
namespace docenhance::tests {
inline std::size_t binary_partial_bytes(std::uint32_t extent, std::uint32_t step) {
    const auto parts = (extent / step) + static_cast<std::uint32_t>(extent % step != 0);
    const auto row = std::size_t{parts} * sizeof(std::uint64_t);
    return 2 *
           (((row + core::buffer_alignment - 1) / core::buffer_alignment) * core::buffer_alignment);
}
inline std::size_t binary_expected_scratch(const methods::FixedThreshold& /*method*/,
                                           std::uint32_t /*width*/, std::uint32_t height,
                                           unsigned /*workers*/) {
    constexpr std::uint32_t band_rows = 64;
    return binary_partial_bytes(height, band_rows);
}
inline std::size_t binary_expected_scratch(const methods::Otsu& /*method*/, std::uint32_t width,
                                           std::uint32_t height, unsigned workers) {
    constexpr std::size_t histogram = 4096 * sizeof(std::uint64_t);
    return histogram + binary_expected_scratch(methods::FixedThreshold::create().value(), width,
                                               height, workers);
}
inline std::size_t binary_expected_scratch(const methods::Sauvola& method, std::uint32_t width,
                                           std::uint32_t /*height*/, unsigned workers) {
    constexpr std::uint32_t strip_columns = 4096;
    const auto strips =
        (width / strip_columns) + static_cast<std::uint32_t>(width % strip_columns != 0);
    const auto slots = std::min(workers, strips);
    const auto columns = std::min(width, strip_columns + method.window() - 1);
    const auto row = std::size_t{columns} * sizeof(std::uint64_t);
    const auto stride =
        ((row + core::buffer_alignment - 1) / core::buffer_alignment) * core::buffer_alignment;
    return (std::size_t{2} * slots * stride) + binary_partial_bytes(width, strip_columns);
}
inline void binary_output_padding(image::PlaneView<const std::uint8_t> output) {
    const auto storage = output.storage();
    for (std::size_t i = 0; i < storage.size(); ++i) {
        if (i % output.row_pitch() >= output.width()) {
            require(storage.subspan(i, 1).front() == binary_sentinel,
                    "complete binary output padding remains entering sentinel");
        }
    }
}
template <typename Method>
void binary_resource_case(const Method& method, std::uint32_t width, std::uint32_t height,
                          unsigned workers) {
    BinaryFixture fixture{width, height};
    const auto input = fixture.codes.view().storage();
    const std::vector<std::uint16_t> before(input.begin(), input.end());
    const auto resident = fixture.budget.used();
    const auto needed = binary_expected_scratch(method, width, height, workers);
    const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
    for (const auto capacity : {needed - 1, needed}) {
        std::ranges::fill(fixture.out.view().storage(), binary_sentinel);
        core::Budget scratch{capacity};
        const auto result = methods::binarize(
            method, {fixture.codes.view().as_const(), fixture.out.view(), scratch, scheduler});
        require(scratch.used() == 0 && fixture.budget.used() == resident &&
                    std::ranges::equal(input, before),
                "actual scratch and resident owners refund correctly");
        if (capacity < needed) {
            require(!result && result.error().code == core::ErrorCode::resource &&
                        std::ranges::all_of(fixture.out.view().storage(),
                                            [](auto value) { return value == binary_sentinel; }),
                    "one byte short refuses complete simultaneous scratch before any store");
        } else {
            require(result && scratch.peak() == needed &&
                        result->black_samples + result->white_samples ==
                            std::uint64_t{width} * height,
                    "exact independent scratch capacity completes real work");
            binary_output_padding(fixture.out.view().as_const());
        }
        std::cout << "Binary scratch: width=" << width << " height=" << height
                  << " workers=" << workers << " expected=" << needed << " capacity=" << capacity
                  << " peak=" << scratch.peak() << " success=" << result.has_value()
                  << " refunded=" << (scratch.used() == 0) << "\n";
    }
}
} // namespace docenhance::tests
