// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "docenhance/methods/binary_facts.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
namespace docenhance::methods {
inline constexpr std::uint64_t binary_code_max = UINT16_MAX;
inline constexpr std::uint32_t binary_band_rows = 64;
inline constexpr std::uint32_t binary_strip_columns = 4096;
inline constexpr std::size_t binary_checkpoint_samples = 1024;
enum class BinaryPartition { rows, strips };
[[nodiscard]] std::uint32_t binary_otsu_bin(std::uint16_t sample) noexcept;
[[nodiscard]] std::uint32_t binary_parts(BinaryInput input, BinaryPartition partition) noexcept;
[[nodiscard]] core::Result<void> validate_binary_input(BinaryInput input);
[[nodiscard]] core::Result<image::Plane<std::uint64_t>> binary_counts(BinaryInput input,
                                                                      BinaryPartition partition);
[[nodiscard]] core::Result<ClassificationFacts>
binary_fold(BinaryInput input, image::PlaneView<const std::uint64_t> counts,
            BinaryPartition partition);
[[nodiscard]] core::Result<void> validate_binary_counts(const ClassificationFacts& counts,
                                                        BinaryInput input);
template <typename Classify>
core::Result<void> binary_write_row(BinaryInput input, std::uint32_t y, ClassificationFacts& facts,
                                    const Classify& classify) {
    const auto cancellation = input.scheduler.get().cancellation();
    const auto source = input.codes.row(y);
    const auto destination = input.out.row(y);
    for (std::size_t x = 0; x < source.size(); ++x) {
        if (x % binary_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        const bool black = classify(source.subspan(x, 1).front());
        destination.subspan(x, 1).front() = black ? std::uint8_t{0} : std::uint8_t{1};
        facts.black_samples += static_cast<std::uint64_t>(black);
        facts.white_samples += static_cast<std::uint64_t>(!black);
    }
    return {};
}
template <typename Classify>
core::Result<ClassificationFacts>
binary_rows(BinaryInput input, image::PlaneView<std::uint64_t> counts, const Classify& classify) {
    const auto task = [&](std::size_t index) -> core::Result<void> {
        const auto first = static_cast<std::uint32_t>(index * binary_band_rows);
        const auto end = first + std::min(binary_band_rows, input.codes.height() - first);
        ClassificationFacts facts;
        for (auto y = first; y < end; ++y) {
            if (auto row = binary_write_row(input, y, facts, classify); !row) {
                return row;
            }
        }
        counts.row(0).subspan(index, 1).front() = facts.black_samples;
        counts.row(1).subspan(index, 1).front() = facts.white_samples;
        return {};
    };
    if (auto mapped = input.scheduler.get().for_each(counts.width(), exec::WorkRef{task});
        !mapped) {
        return std::unexpected(mapped.error());
    }
    return binary_fold(input, counts.as_const(), BinaryPartition::rows);
}
} // namespace docenhance::methods
