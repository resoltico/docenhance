// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
namespace docenhance::methods {
namespace {
double value_at(image::PlaneView<const double> input, ExtremaPass pass, std::uint32_t line,
                std::uint64_t position) noexcept {
    const auto length = pass.horizontal ? input.width() : input.height();
    const auto index =
        image::reflect101_folded(static_cast<std::int64_t>(position) - pass.radius, length);
    return pass.horizontal ? input.row(line).subspan(index, 1).front()
                           : input.row(static_cast<std::uint32_t>(index)).subspan(line, 1).front();
}
bool dominates(double incoming, double previous, bool dilation) noexcept {
    return dilation ? incoming >= previous : incoming <= previous;
}
struct LineStorage {
    image::PlaneView<const double> input;
    image::PlaneView<double> output;
    std::span<std::uint64_t> queue;
};
void store(LineStorage storage, ExtremaPass pass, std::uint32_t line, std::uint32_t index,
           double value) noexcept {
    if (pass.horizontal) {
        storage.output.row(line).subspan(index, 1).front() = value;
    } else {
        storage.output.row(index).subspan(line, 1).front() = value;
    }
}
core::Result<void> line_pass(LineStorage storage, ExtremaPass pass, std::uint32_t line,
                             const core::Cancellation& cancellation) {
    const auto input = storage.input;
    const auto queue = storage.queue;
    const auto length = pass.horizontal ? input.width() : input.height();
    const std::uint64_t diameter = std::uint64_t{2} * pass.radius;
    const auto capacity = static_cast<std::size_t>(diameter + 1);
    std::size_t first = 0;
    std::size_t count = 0;
    constexpr std::uint64_t checkpoint_interval = 256;
    for (std::uint64_t p = 0; p < std::uint64_t{length} + diameter; ++p) {
        if (p % checkpoint_interval == 0 && cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        const auto value = value_at(input, pass, line, p);
        if (!std::isfinite(value) || value < 0 || value > 1) {
            return core::failure(core::ErrorCode::numerical,
                                 "I02 extrema input is not unit finite luminance");
        }
        while (count != 0 && queue.subspan(first, 1).front() + diameter < p) {
            first = (first + 1) % capacity;
            --count;
        }
        while (count != 0) {
            const auto back = (first + count - 1) % capacity;
            if (!dominates(value, value_at(input, pass, line, queue.subspan(back, 1).front()),
                           pass.dilation)) {
                break;
            }
            --count;
        }
        queue.subspan((first + count) % capacity, 1).front() = p;
        ++count;
        if (p >= diameter) {
            const auto index = static_cast<std::uint32_t>(p - diameter);
            const auto chosen = value_at(input, pass, line, queue.subspan(first, 1).front());
            store(storage, pass, line, index, chosen);
        }
    }
    return {};
}
} // namespace
core::Result<void> extrema_pass(image::PlaneView<const double> input,
                                image::PlaneView<double> output, std::span<std::uint64_t> queue,
                                ExtremaPass pass, const core::Cancellation& cancellation) {
    if (input.empty() || input.width() != output.width() || input.height() != output.height() ||
        image::overlaps(input, output) || image::overlaps(queue, input.storage()) ||
        image::overlaps(queue, output.storage()) || pass.radius < Morphology::min_radius ||
        pass.radius > Morphology::max_radius || queue.size() < (2 * pass.radius) + 1) {
        return core::failure(core::ErrorCode::argument, "Invalid I02 extrema pass storage");
    }
    const auto lines = pass.horizontal ? input.height() : input.width();
    for (std::uint32_t line = 0; line < lines; ++line) {
        auto result =
            line_pass({.input = input, .output = output, .queue = queue}, pass, line, cancellation);
        if (!result) {
            return result;
        }
    }
    return {};
}
} // namespace docenhance::methods
