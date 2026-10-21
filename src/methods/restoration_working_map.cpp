// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/working.hpp"
#include "restoration_working_detail.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <utility>
namespace docenhance::methods {
namespace {
enum class Count : std::size_t {
    evaluated,
    corrected,
    changed,
    raw_low,
    raw_high,
    blended_low,
    blended_high,
    size,
};
using Counts = std::array<std::uint64_t, static_cast<std::size_t>(Count::size)>;
struct Mapping {
    Mapping(Working source, const Wiener& selected, image::PlaneView<const double> values,
            RestorationTransform geometry) noexcept
        : working(source), method(selected), frame(values), transform(geometry) {}

    Working working;
    std::reference_wrapper<const Wiener> method;
    image::PlaneView<const double> frame;
    RestorationTransform transform;
};
void increment(Counts& counts, Count field, bool value = true) noexcept {
    std::span{counts}.subspan(static_cast<std::size_t>(field), 1).front() +=
        static_cast<std::uint64_t>(value);
}
core::Result<void> map_row(const Mapping& m, std::uint32_t y, Counts& counts) {
    const auto target = m.working.luminance.row(y);
    const auto source =
        m.frame.row(y + m.transform.guard).subspan(m.transform.guard, target.size());
    const auto mask = m.working.protection.empty() ? std::span<const std::uint8_t>{}
                                                   : m.working.protection.row(y);
    const auto cancellation = m.working.scheduler.get().cancellation();
    for (std::size_t x = 0; x < target.size(); ++x) {
        if (x % restoration_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        if (!mask.empty() && mask.subspan(x, 1).front() != 0) {
            continue;
        }
        auto& original = target.subspan(x, 1).front();
        const auto candidate = source.subspan(x, 1).front() + m.transform.padded_mean;
        if (!std::isfinite(candidate)) {
            return core::failure(core::ErrorCode::numerical, "Nonfinite restored candidate");
        }
        const auto blended = restoration_target(original, candidate, m.method.get());
        if (!std::isfinite(blended)) {
            return core::failure(core::ErrorCode::numerical, "Restoration blend is not finite");
        }
        increment(counts, Count::evaluated);
        increment(counts, Count::corrected, blended != original);
        increment(counts, Count::raw_low, candidate < 0);
        increment(counts, Count::raw_high, candidate > 1);
        increment(counts, Count::blended_low, blended < 0);
        increment(counts, Count::blended_high, blended > 1);
        const auto value = std::clamp(blended, 0.0, 1.0);
        if (value != original) {
            original = value;
            increment(counts, Count::changed);
        }
    }
    return {};
}
void fold(WienerFacts& facts, const Counts& counts) noexcept {
    facts.counts.evaluated +=
        std::span{counts}.subspan(static_cast<std::size_t>(Count::evaluated), 1).front();
    facts.counts.changed +=
        std::span{counts}.subspan(static_cast<std::size_t>(Count::changed), 1).front();
    facts.corrected_samples +=
        std::span{counts}.subspan(static_cast<std::size_t>(Count::corrected), 1).front();
    facts.raw_low_samples +=
        std::span{counts}.subspan(static_cast<std::size_t>(Count::raw_low), 1).front();
    facts.raw_high_samples +=
        std::span{counts}.subspan(static_cast<std::size_t>(Count::raw_high), 1).front();
    facts.blended_low_samples +=
        std::span{counts}.subspan(static_cast<std::size_t>(Count::blended_low), 1).front();
    facts.blended_high_samples +=
        std::span{counts}.subspan(static_cast<std::size_t>(Count::blended_high), 1).front();
}
} // namespace
core::Result<WienerFacts> restoration_map(Working working, const Wiener& method,
                                          image::PlaneView<const double> frame,
                                          RestorationTransform transform, WienerFacts facts) {
    const auto bands = restoration_bands(working.luminance.height());
    const auto cancellation = working.scheduler.get().cancellation();
    if (cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto observations = image::Plane<std::uint64_t>::allocate(
        working.budget.get(), bands, static_cast<std::uint32_t>(Count::size));
    if (!observations) {
        return std::unexpected(std::move(observations.error()));
    }
    const Mapping mapping{working, method, frame, transform};
    const auto task = [&](std::size_t index) -> core::Result<void> {
        const auto band = restoration_band(working.luminance.height(), index);
        Counts counts{};
        for (auto y = band.first; y < band.first + band.rows; ++y) {
            if (auto row = map_row(mapping, y, counts); !row) {
                return row;
            }
        }
        for (std::size_t field = 0; field < counts.size(); ++field) {
            observations->view().row(static_cast<std::uint32_t>(field)).subspan(index, 1).front() =
                counts.at(field);
        }
        return {};
    };
    if (auto mapped = working.scheduler.get().for_each(bands, exec::WorkRef{task}); !mapped) {
        return std::unexpected(std::move(mapped.error()));
    }
    for (std::uint32_t index = 0; index < bands; ++index) {
        if (index % restoration_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        Counts counts{};
        for (std::size_t field = 0; field < counts.size(); ++field) {
            counts.at(field) = observations->view()
                                   .row(static_cast<std::uint32_t>(field))
                                   .subspan(index, 1)
                                   .front();
        }
        fold(facts, counts);
    }
    facts.disposition = facts.counts.changed == 0 ? Disposition::no_change : Disposition::applied;
    facts.reason = facts.counts.changed == 0 ? WienerReason::no_effect : WienerReason::none;
    return facts;
}
} // namespace docenhance::methods
