// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "require.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>
namespace docenhance::tests {
template <typename Method>
void binary_alias_entry(const Method& method, methods::BinaryInput input) {
    const auto bytes = std::as_bytes(input.codes.storage());
    const std::vector<std::byte> before(bytes.begin(), bytes.end());
    const auto held = input.budget.get().used();
    const CheckpointStop stop{core::Checkpoint::allocation, 0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result =
        methods::binarize(method, {input.codes, input.out, input.budget, scheduler});
    require(!result && result.error().code == core::ErrorCode::argument &&
                CheckpointStop::visits() == 0 && input.budget.get().used() == held &&
                std::ranges::equal(bytes, before),
            "actual binary overlap refuses before access stop acquisition or store");
}
template <typename Method>
static void binary_disjoint_entry(const Method& method,
                                  image::PlaneView<const std::uint16_t> codes) {
    core::Budget budget{std::size_t{1024} * 1024};
    std::array<std::uint8_t, 1> output{17};
    const auto out =
        image::PlaneView<std::uint8_t>::create(output, {.width = 1, .height = 1, .stride = 1})
            .value();
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto result = methods::binarize(method, {codes, out, budget, scheduler});
    require(result && result->white_samples == 1 && result->black_samples == 0 &&
                output.front() == 1 && budget.used() == 0,
            "actual same-geometry disjoint binary control classifies and refunds");
}
inline void binary_input_alias_cases() {
    static_assert(std::is_same_v<std::uint8_t, unsigned char> && sizeof(std::uint16_t) == 2);
    constexpr std::array<std::uint16_t, 2> initial{32768, 0x3141};
    constexpr auto storage_bytes = sizeof(std::uint16_t) * initial.size();
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    alignas(std::uint16_t) unsigned char storage[storage_bytes]{};
    const std::span bytes{storage};
    auto* const typed =
        static_cast<std::uint16_t*>(std::memcpy(bytes.data(), initial.data(), bytes.size()));
    const std::span<const std::uint16_t> samples{typed, initial.size()};
    const auto codes = image::PlaneView<const std::uint16_t>::create(
                           samples, {.width = 1, .height = 1, .stride = 4})
                           .value();
    const auto otsu = methods::Otsu::create();
    const auto sauvola = methods::Sauvola::create().value();
    const auto fixed = methods::FixedThreshold::create().value();
    binary_disjoint_entry(otsu, codes);
    binary_disjoint_entry(sauvola, codes);
    binary_disjoint_entry(fixed, codes);
    core::Budget budget{0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    for (const auto offset : {std::size_t{0}, sizeof(std::uint16_t)}) {
        const auto out = image::PlaneView<std::uint8_t>::create(
            bytes.subspan(offset, 1), {.width = 1, .height = 1, .stride = 1});
        require(out && codes.shape().stride == 4 && codes.storage().size() == 2 &&
                    out->storage().size() == 1,
                "actual direct and padding-only byte ranges have independently valid geometry");
        binary_alias_entry(otsu, {codes, *out, budget, scheduler});
        binary_alias_entry(sauvola, {codes, *out, budget, scheduler});
        binary_alias_entry(fixed, {codes, *out, budget, scheduler});
    }
}
} // namespace docenhance::tests
