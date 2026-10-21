// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "binary_detail.hpp"
#include "binary_fixture.hpp"
#include "binary_large_window_reference.hpp"
#include "binary_numerical_cases.hpp"
#include "binary_reference.hpp"
#include "binary_resource_cases.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "require.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>
namespace docenhance::tests {
TEST_CASE("Binary Q16 fixed Otsu and Sauvola match independent numerical definitions") {
    const auto before = assertion_counter().load();
    binary_numerical_cases();
    REQUIRE(assertion_counter().load() > before);
}
TEST_CASE("Binary Q16 full code domain fills all4096 bins and preserves old embeddings") {
    const auto before = assertion_counter().load();
    BinaryFixture fixture{65536, 1};
    const auto row = fixture.codes.view().row(0);
    for (std::uint32_t i = 0; i < 65536; ++i) {
        const auto code = static_cast<std::uint16_t>(i);
        row.subspan(i, 1).front() = code;
        require(
            methods::binary_otsu_bin(code) == binary_reference_bin(code),
            "actual exact integer bin matches independent floating quantizer over full Q domain");
    }
    binary_otsu_case(fixture);
    REQUIRE(assertion_counter().load() > before);
}
namespace {
void check_large_window_seams(const BinaryFixture& fixture, const methods::Sauvola& method) {
    constexpr std::uint32_t width = 32777;
    unsigned black = 0;
    unsigned white = 0;
    for (std::uint32_t y = 0; y < 2; ++y) {
        for (const auto x : {
                 0U,
                 997U,
                 1994U,
                 2047U,
                 2048U,
                 2991U,
                 4095U,
                 4096U,
                 4097U,
                 6143U,
                 8191U,
                 8192U,
                 10240U,
                 16383U,
                 width - 2,
                 width - 1,
             }) {
            const auto expected = binary_large_window_reference(fixture.codes.view(), method, x, y);
            require(fixture.out.view().row(y).subspan(x, 1).front() == expected,
                    "actual maxradius borders/seams match independently enumerated multiplicities");
            black += static_cast<unsigned>(expected == 0);
            white += static_cast<unsigned>(expected == 1);
        }
    }
    for (std::uint32_t y = 0; y < 2; ++y) {
        for (std::uint32_t boundary = 4096; boundary < width; boundary += 4096) {
            for (const auto x : {boundary - 1, boundary, boundary + 1}) {
                const auto expected =
                    binary_large_window_reference(fixture.codes.view(), method, x, y);
                require(fixture.out.view().row(y).subspan(x, 1).front() == expected,
                        "every actual nine-strip seam neighbor matches independent math");
            }
        }
    }
    require(black != 0 && white != 0,
            "large-window seam controls attain both classification decisions");
}
void large_window_seam_case() {
    constexpr std::uint32_t width = 32777;
    BinaryFixture fixture{width, 2};
    constexpr std::uint32_t block = 997;
    constexpr std::uint32_t row_shift = 613;
    for (std::uint32_t y = 0; y < 2; ++y) {
        const auto row = fixture.codes.view().row(y);
        for (std::uint32_t x = 0; x < width; ++x) {
            row.subspan(x, 1).front() = static_cast<std::uint16_t>(
                65534U + static_cast<unsigned>(((x + (row_shift * y)) / block) % 4 < 2));
        }
    }
    const auto source = fixture.codes.view().storage();
    const std::vector<std::uint16_t> before(source.begin(), source.end());
    const auto resident = fixture.budget.used();
    const auto method = methods::Sauvola::create({.window = 4095, .k = 0.5 / 65535.0}).value();
    std::vector<std::uint8_t> baseline;
    for (const auto workers : {1U, 2U, 7U}) {
        const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
        const auto result =
            methods::binarize(method, {fixture.codes.view().as_const(), fixture.out.view(),
                                       fixture.budget, scheduler});
        require(result && fixture.budget.used() == resident && std::ranges::equal(source, before),
                "actual maximum-window nine-strip work preserves complete source and refunds");
        check_large_window_seams(fixture, method);
        binary_output_padding(fixture.out.view().as_const());
        const auto output = fixture.out.view().storage();
        if (workers == 1) {
            baseline.assign(output.begin(), output.end());
        } else {
            require(std::ranges::equal(output, baseline),
                    "actual full maxwindow output is worker invariant");
        }
    }
}
} // namespace
TEST_CASE("Binary Q16 maximum-window seams match independent actual multiplicities") {
    const auto before = assertion_counter().load();
    large_window_seam_case();
    REQUIRE(assertion_counter().load() > before);
}
} // namespace docenhance::tests
