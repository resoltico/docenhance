// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "png_fixture.hpp"
#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace {
docenhance::tests::GrayFixture fixture(docenhance::fuzz::FuzzInput& input) {
    constexpr auto depths = std::to_array<unsigned>({1, 2, 4, 8});
    docenhance::tests::GrayFixture result{
        .width = 1U + (input.byte() % 32U),
        .height = 1U + (input.byte() % 32U),
        .depth = depths.at(input.byte() % depths.size()),
        .interlaced = input.byte() % 2U != 0,
        .filter = input.byte() % 5U,
        .samples = {},
    };
    const unsigned mask = (1U << result.depth) - 1U;
    for (std::uint32_t index = 0; index < result.width * result.height; ++index) {
        result.samples.push_back(static_cast<std::uint8_t>(input.byte() & mask));
    }
    return result;
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using docenhance::fuzz::require;
    docenhance::fuzz::FuzzInput input{std::span<const std::uint8_t>{data, size}};
    const auto expected = fixture(input);
    const auto encoded = docenhance::tests::make_gray_png(expected);
    docenhance::core::Budget budget{std::size_t{8} * 1024 * 1024};
    {
        const auto result = docenhance::io::decode_grayscale_png(
            encoded, budget, {.encoded_bytes = 65536, .pixels = 4096});
        require(result.has_value(), "independently encoded grayscale PNG is accepted");
        require(result->width() == expected.width && result->height() == expected.height,
                "PNG dimensions agree with the independently constructed fixture");
        const unsigned maximum = (1U << expected.depth) - 1U;
        const auto decoded_view = result->view();
        for (std::uint32_t y = 0; y < expected.height; ++y) {
            std::uint32_t x = 0;
            for (const auto decoded_sample : decoded_view.row(y)) {
                const unsigned sample = expected.samples.at((y * expected.width) + x);
                ++x;
                require(decoded_sample == sample * 255U / maximum,
                        "every stored sample survives expansion and Adam7 pass assembly exactly");
            }
        }
    }
    require(budget.used() == 0, "successful sample decoding refunds all charged allocations");
    return 0;
}
