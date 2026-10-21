// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "binary_detail.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "sauvola_working_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
namespace docenhance::methods {
namespace {
struct StripWork {
    StripWork(BinaryInput source, const Sauvola& selected,
              image::PlaneView<std::uint64_t> workspace,
              image::PlaneView<std::uint64_t> observations, unsigned active) noexcept
        : input(source), method(selected), moments(workspace), counts(observations), slots(active) {
    }
    BinaryInput input;
    std::reference_wrapper<const Sauvola> method;
    image::PlaneView<std::uint64_t> moments;
    image::PlaneView<std::uint64_t> counts;
    unsigned slots;
    core::Result<void> operator()(std::size_t slot) const {
        const auto radius = method.get().window() / 2;
        for (auto index = slot; index < counts.width(); index += slots) {
            const auto first = static_cast<std::uint32_t>(index) * binary_strip_columns;
            const auto end = first + std::min(binary_strip_columns, input.codes.width() - first);
            const auto source_first = first - std::min(radius, first);
            const auto source_end = end + std::min(radius, input.codes.width() - end);
            const auto columns = source_end - source_first;
            auto result = binary_sauvola_strip({
                input,
                method.get(),
                first,
                end,
                {
                    .sums = moments.row(static_cast<std::uint32_t>(2 * slot)).first(columns),
                    .squares =
                        moments.row(static_cast<std::uint32_t>((2 * slot) + 1)).first(columns),
                    .first = source_first,
                },
            });
            if (!result) {
                return std::unexpected(result.error());
            }
            counts.row(0).subspan(index, 1).front() = result->black_samples;
            counts.row(1).subspan(index, 1).front() = result->white_samples;
        }
        return {};
    }
};
} // namespace
core::Result<SauvolaFacts> binarize(const Sauvola& method, BinaryInput input) {
    if (auto valid = validate_binary_input(input); !valid) {
        return std::unexpected(valid.error());
    }
    const auto slots =
        std::min(input.scheduler.get().workers(), binary_parts(input, BinaryPartition::strips));
    const auto columns = std::min(input.codes.width(), binary_strip_columns + method.window() - 1);
    if (input.scheduler.get().cancellation().requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto moments = image::Plane<std::uint64_t>::allocate(input.budget.get(), columns, 2 * slots);
    if (!moments) {
        return std::unexpected(moments.error());
    }
    auto counts = binary_counts(input, BinaryPartition::strips);
    if (!counts) {
        return std::unexpected(counts.error());
    }
    const StripWork work{input, method, moments->view(), counts->view(), slots};
    if (auto classified = input.scheduler.get().for_each(slots, exec::WorkRef{work}); !classified) {
        return std::unexpected(classified.error());
    }
    auto folded = binary_fold(input, counts->view().as_const(), BinaryPartition::strips);
    if (!folded) {
        return std::unexpected(folded.error());
    }
    SauvolaFacts facts;
    static_cast<ClassificationFacts&>(facts) = *folded;
    return facts;
}
} // namespace docenhance::methods
