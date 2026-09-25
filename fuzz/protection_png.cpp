// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/io/protection_png.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
namespace {
constexpr std::size_t budget_limit = std::size_t{4} * 1024 * 1024;
constexpr std::uint32_t maximum_extent = 32;
std::uint32_t extent_at(std::span<const std::uint8_t> bytes, std::size_t at) {
    constexpr std::size_t word = 4;
    if (bytes.size() < at + word) {
        return 1;
    }
    std::uint32_t value = 0;
    constexpr unsigned shift = 8;
    for (const auto byte : bytes.subspan(at, word)) {
        value = (value << shift) | byte;
    }
    return value == 0 || value > maximum_extent ? 1 : value;
}
void check_samples(docenhance::image::PlaneView<const std::uint8_t> view) {
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        docenhance::fuzz::require(
            std::ranges::all_of(view.row(y), [](auto p) { return p == 0 || p == 1; }),
            "normalized mask samples");
    }
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace core = docenhance::core;
    namespace image = docenhance::image;
    namespace io = docenhance::io;
    using docenhance::fuzz::require;
    const std::span<const std::uint8_t> bytes{data, size};
    constexpr std::size_t width_at = 16;
    constexpr std::size_t height_at = 20;
    const image::Extent extent{
        .width = extent_at(bytes, width_at),
        .height = extent_at(bytes, height_at),
    };
    core::Budget budget{budget_limit};
    {
        auto decoded = io::decode_protection_png(bytes, extent, budget);
        if (decoded) {
            require(decoded->width() == extent.width && decoded->height() == extent.height,
                    "mask extent");
            check_samples(decoded->view().as_const());
        }
    }
    require(budget.used() == 0, "mask decoding refunds all ownership");
    return 0;
}
