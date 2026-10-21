// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>

namespace docenhance::fuzz {
// Concrete test-only readback. The shared callback updates after its joined work; no reset or
// concurrent observer is supported. No production flag or callback seam is introduced.
struct RenderHarnessCounts {
    std::uint64_t entry_calls{};
    std::uint64_t render_calls{};
    std::uint64_t healthy_completions{};
    std::uint64_t output_comparisons{};
    std::uint64_t input_refusals{};
    std::uint64_t moved_refusals{};
    std::uint64_t refunded_cases{};
};
[[nodiscard]] RenderHarnessCounts render_harness_counts() noexcept;
} // namespace docenhance::fuzz
