// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "binary_fixture.hpp"
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "require.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace docenhance::tests {
namespace {
template <typename Method>
void reject_binary_geometry(const Method& method, image::PlaneView<const std::uint16_t> codes,
                            image::PlaneView<std::uint8_t> output) {
    core::Budget budget{0};
    const CheckpointStop stop{core::Checkpoint::allocation, 0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(7, 7, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = methods::binarize(method, {codes, output, budget, scheduler});
    require(!result && result.error().code == core::ErrorCode::argument &&
                CheckpointStop::visits() == 0 && budget.used() == 0,
            "actual invalid geometry refuses before pending stop or workspace acquisition");
}
void reject_all_binary_geometry(image::PlaneView<const std::uint16_t> codes,
                                image::PlaneView<std::uint8_t> output) {
    reject_binary_geometry(methods::Otsu::create(), codes, output);
    reject_binary_geometry(methods::Sauvola::create().value(), codes, output);
    reject_binary_geometry(methods::FixedThreshold::create().value(), codes, output);
}
} // namespace
TEST_CASE("Binary Q16 empty mismatch and actual over40M views reject before effects") {
    const auto before = assertion_counter().load();
    const std::array<std::uint16_t, 4> small{};
    std::array<std::uint8_t, 8> output{};
    output.fill(binary_sentinel);
    const auto codes =
        image::PlaneView<const std::uint16_t>::create(small, {.width = 2, .height = 2, .stride = 4})
            .value();
    const auto mismatch =
        image::PlaneView<std::uint8_t>::create(output, {.width = 3, .height = 2, .stride = 4})
            .value();
    const image::PlaneView<const std::uint16_t> empty_codes;
    const image::PlaneView<std::uint8_t> empty_out;
    reject_all_binary_geometry(empty_codes, mismatch);
    reject_all_binary_geometry(codes, empty_out);
    reject_all_binary_geometry(codes, mismatch);
    require(std::ranges::all_of(output, [](auto value) { return value == binary_sentinel; }),
            "all malformed small geometry leaves full destination unchanged");
    static_assert(image::source_pixels_max < UINT32_MAX);
    const auto too_many = static_cast<std::uint32_t>(image::source_pixels_max + 1);
    const std::vector<std::uint16_t> large_codes(too_many, binary_padding);
    std::vector<std::uint8_t> large_out(too_many, binary_sentinel);
    const auto source = image::PlaneView<const std::uint16_t>::create(
                            large_codes,
                            {
                                .width = too_many,
                                .height = 1,
                                .stride = std::size_t{too_many} * sizeof(std::uint16_t),
                            })
                            .value();
    const auto destination = image::PlaneView<std::uint8_t>::create(
                                 large_out, {.width = too_many, .height = 1, .stride = too_many})
                                 .value();
    reject_all_binary_geometry(source, destination);
    require(std::ranges::all_of(large_codes, [](auto value) { return value == binary_padding; }) &&
                std::ranges::all_of(large_out, [](auto value) { return value == binary_sentinel; }),
            "genuinely allocated over-limit backing remains unchanged without forging a range");
    REQUIRE(assertion_counter().load() > before);
}
} // namespace docenhance::tests
