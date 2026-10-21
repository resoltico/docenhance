// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binary_facts.hpp"

#include <cstdint>
#include <functional>
namespace docenhance::methods {
struct BinaryInput {
    BinaryInput(image::PlaneView<const std::uint16_t> source,
                image::PlaneView<std::uint8_t> destination,
                std::reference_wrapper<core::Budget> memory,
                std::reference_wrapper<const exec::Scheduler> control) noexcept
        : codes(source), out(destination), budget(memory), scheduler(control) {}
    image::PlaneView<const std::uint16_t> codes;
    image::PlaneView<std::uint8_t> out;
    std::reference_wrapper<core::Budget> budget;
    std::reference_wrapper<const exec::Scheduler> scheduler;
};
[[nodiscard]] core::Result<OtsuFacts> binarize(const Otsu& method, BinaryInput input);
[[nodiscard]] core::Result<SauvolaFacts> binarize(const Sauvola& method, BinaryInput input);
[[nodiscard]] core::Result<FixedFacts> binarize(const FixedThreshold& method, BinaryInput input);
} // namespace docenhance::methods
