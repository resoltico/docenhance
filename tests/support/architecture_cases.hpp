// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/fixed_threshold.hpp"
#include "require.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace docenhance::tests {
inline void ownership_cases() {
    core::Buffer survivor;
    {
        core::Budget ephemeral{512};
        survivor = std::move(ephemeral.allocate(128).value());
        survivor.bytes().front() = std::byte{42};
        require(ephemeral.used() == 128, "the shared ledger charges the buffer");
    }
    require(survivor.bytes().front() == std::byte{42}, "an owning buffer may outlive its budget");
    core::Budget budget{512};
    auto original = image::Plane<std::uint8_t>::allocate(budget, 4, 1).value();
    const auto moved = std::move(original);
    // The move operation explicitly guarantees an empty source. This is that regression.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    require(original.empty() && original.width() == 0 && original.view().empty(),
            "a moved-from plane has an empty shape as well as empty storage");
    require(!methods::fixed_threshold(moved.view(), original.view(), 0.5),
            "a moved-from destination is rejected before row access");
    survivor = core::Buffer{};
    require(survivor.empty(), "the final owner safely releases the escaped ledger");
}
inline void view_cases() {
    std::array<float, 16> storage{};
    using View = image::PlaneView<float>;
    static_assert(!std::is_constructible_v<View, std::span<float>, image::PlaneShape>);
    require(!View::create(storage, {.width = 0, .height = 1, .stride = 4}),
            "zero width is rejected");
    require(!View::create(storage, {.width = 1, .height = 0, .stride = 4}),
            "zero height is rejected");
    require(!View::create(storage, {.width = 1, .height = 1, .stride = 3}),
            "fractional sample strides are rejected");
    require(!View::create(storage, {.width = 2, .height = 1, .stride = 4}),
            "short strides are rejected");
    require(!View::create(storage, {.width = 4, .height = 5, .stride = 16}),
            "short storage is rejected");
    require(!View::create(storage,
                          {
                              .width = 1,
                              .height = std::numeric_limits<std::uint32_t>::max(),
                              .stride = std::numeric_limits<std::size_t>::max() - 3,
                          }),
            "hostile view extents fail without arithmetic overflow");
    const auto partial =
        View::create(std::span{storage}.first(9), {.width = 1, .height = 3, .stride = 16});
    require(partial.has_value() && partial->row(2).size() == 1,
            "last-row trailing padding need not exist");
    const auto first =
        View::create(std::span{storage}.first(8), {.width = 4, .height = 2, .stride = 16}).value();
    const auto second =
        View::create(std::span{storage}.last(8), {.width = 4, .height = 2, .stride = 16}).value();
    const auto overlapping =
        View::create(std::span{storage}.subspan(1, 8), {.width = 4, .height = 2, .stride = 16})
            .value();
    require(!image::overlaps(first, second), "adjacent disjoint views do not overlap");
    require(image::overlaps(first, overlapping), "partial overlap is found in either direction");
    require(image::overlaps(overlapping, first), "overlap checks are symmetric");
}
inline void threshold_cases() {
    std::array<std::uint8_t, 256> source{};
    std::array<std::uint8_t, 256> output{};
    for (std::size_t index = 0; index < source.size(); ++index) {
        source.at(index) = static_cast<std::uint8_t>(index);
    }
    const auto in = image::PlaneView<const std::uint8_t>::create(
                        source, {.width = 256, .height = 1, .stride = 256})
                        .value();
    const auto out =
        image::PlaneView<std::uint8_t>::create(output, {.width = 256, .height = 1, .stride = 256})
            .value();
    require(methods::fixed_threshold(in, out, 0.5).has_value(),
            "B03 processes the complete sample domain");
    for (std::size_t index = 0; index < output.size(); ++index) {
        require(output.at(index) == (index <= 127 ? 0 : 255),
                "B03 matches the mathematical threshold");
    }
    const auto alias =
        image::PlaneView<std::uint8_t>::create(source, {.width = 256, .height = 1, .stride = 256})
            .value();
    require(!methods::fixed_threshold(in, alias, 0.5), "B03 rejects aliasing before writing");
    require(source.front() == 0 && source.back() == 255, "alias rejection does not mutate input");
    require(!methods::fixed_threshold(in, out, std::numeric_limits<double>::quiet_NaN()),
            "NaN is not a threshold");
    require(!methods::fixed_threshold(in, out, std::numeric_limits<double>::infinity()),
            "infinity is not a threshold");
    require(!methods::fixed_threshold(in, out, -0.1), "negative thresholds fail");
    require(methods::fixed_threshold(in, out, 0).has_value() && output.at(1) == 255,
            "zero threshold retains nonzero samples");
    require(methods::fixed_threshold(in, out, 1).has_value() && output.back() == 0,
            "one threshold blackens every sample");
}
inline void execution_exception_cases() {
    for (const unsigned workers : {1U, 4U}) {
        const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, workers, 0, 0).value()};
        const auto bad_alloc = [](std::size_t) -> core::Result<void> { throw std::bad_alloc{}; };
        const auto unexpected = [](std::size_t) -> core::Result<void> {
            throw std::runtime_error{"test"};
        };
        require(scheduler.for_each(8, exec::WorkRef{bad_alloc}).error().code ==
                    core::ErrorCode::resource,
                "task allocation failures are values on all execution paths");
        require(scheduler.for_each(8, exec::WorkRef{unexpected}).error().code ==
                    core::ErrorCode::invariant,
                "unexpected task exceptions cannot escape worker threads");
    }
}
inline void admission_cases() {
    static_assert(!std::is_default_constructible_v<app::ProcessRequest>);
    contract::Invocation invocation{};
    invocation.command = contract::Command::process;
    invocation.subject = "input.png";
    invocation.output_directory = "output";
    invocation.binarize = "fixed";
    auto accepted = app::prepare_process(invocation);
    require(accepted.has_value() && accepted->threshold() == 0.5,
            "application admission owns defaults");
    invocation.fixed_threshold = "NaN";
    require(!app::prepare_process(invocation),
            "the processing port cannot receive an invalid threshold");
    invocation.fixed_threshold.clear();
    invocation.output_directory = std::string{"output\0hidden", 13};
    require(!app::prepare_process(invocation), "paths cannot be silently truncated by C APIs");
}
inline void architecture_cases() {
    ownership_cases();
    view_cases();
    threshold_cases();
    execution_exception_cases();
    admission_cases();
}
} // namespace docenhance::tests
