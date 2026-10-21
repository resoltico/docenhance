// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "binary_detail.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "docenhance/methods/binary_facts.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
namespace docenhance::methods {
std::uint32_t binary_parts(BinaryInput input, BinaryPartition partition) noexcept {
    const auto size =
        partition == BinaryPartition::rows ? input.codes.height() : input.codes.width();
    const auto step = partition == BinaryPartition::rows ? binary_band_rows : binary_strip_columns;
    return (size / step) + static_cast<std::uint32_t>(size % step != 0);
}
core::Result<void> validate_binary_input(BinaryInput input) {
    if (input.codes.empty() || input.out.empty() || input.codes.width() != input.out.width() ||
        input.codes.height() != input.out.height() ||
        std::uint64_t{input.codes.width()} * input.codes.height() > image::source_pixels_max ||
        image::overlaps(input.codes, input.out)) {
        return core::failure(core::ErrorCode::argument, "Invalid binary input geometry or overlap");
    }
    return {};
}
core::Result<image::Plane<std::uint64_t>> binary_counts(BinaryInput input,
                                                        BinaryPartition partition) {
    if (input.scheduler.get().cancellation().requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    return image::Plane<std::uint64_t>::allocate(input.budget.get(), binary_parts(input, partition),
                                                 2);
}
core::Result<void> validate_binary_counts(const ClassificationFacts& counts, BinaryInput input) {
    if (!validate(counts) || counts.black_samples + counts.white_samples !=
                                 std::uint64_t{input.codes.width()} * input.codes.height()) {
        return core::failure(core::ErrorCode::invariant, "Incomplete binary classification counts");
    }
    return {};
}
core::Result<ClassificationFacts> binary_fold(BinaryInput input,
                                              image::PlaneView<const std::uint64_t> counts,
                                              BinaryPartition partition) {
    ClassificationFacts facts;
    const auto size =
        partition == BinaryPartition::rows ? input.codes.height() : input.codes.width();
    const auto step = partition == BinaryPartition::rows ? binary_band_rows : binary_strip_columns;
    const auto cross =
        partition == BinaryPartition::rows ? input.codes.width() : input.codes.height();
    for (std::uint32_t i = 0; i < counts.width(); ++i) {
        if (i % binary_checkpoint_samples == 0 &&
            input.scheduler.get().cancellation().requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        const auto expected = std::uint64_t{std::min(step, size - (i * step))} * cross;
        const auto black = counts.row(0).subspan(i, 1).front();
        const auto white = counts.row(1).subspan(i, 1).front();
        if (black > expected || white != expected - black) {
            return core::failure(core::ErrorCode::invariant, "Invalid binary partition counts");
        }
        facts.black_samples += black;
        facts.white_samples += white;
    }
    if (auto valid = validate_binary_counts(facts, input); !valid) {
        return std::unexpected(valid.error());
    }
    return facts;
}
} // namespace docenhance::methods
