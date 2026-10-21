// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "png_memory_output.hpp"

#include "docenhance/codec/encoded_png.hpp"
#include "docenhance/codec/png_output.hpp"
#include "docenhance/codec/png_stream.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>

namespace docenhance::codec {
PngOutput PngMemoryOutput::stream() noexcept {
    return {
        .state = this,
        .open = [](void*) { return true; },
        .write =
            [](void* state, std::span<const std::uint8_t> bytes) noexcept {
                return static_cast<PngMemoryOutput*>(state)->append(bytes);
            },
        .flush = [](void*) noexcept { return true; },
    };
}
bool PngMemoryOutput::grow(std::size_t required) {
    constexpr std::size_t initial_capacity = 8192;
    auto capacity = storage_.empty() ? initial_capacity : storage_.size();
    while (capacity < required) {
        capacity = std::min(core::bundle_max_file_bytes, capacity * 2);
    }
    if (capacity == storage_.size()) {
        return true;
    }
    auto replacement = budget_.get().allocate(capacity);
    if (!replacement) {
        return false;
    }
    // Both capacities remain charged through the copy. Only initialized bytes are read.
    std::ranges::copy(storage_.bytes().first(used_), replacement->bytes().begin());
    storage_ = std::move(*replacement);
    return true;
}
bool PngMemoryOutput::append(std::span<const std::uint8_t> bytes) noexcept {
    try {
        if (exhausted_ || bytes.size() > core::bundle_max_file_bytes - used_ ||
            !grow(used_ + bytes.size())) {
            exhausted_ = true;
            return false;
        }
        std::ranges::copy(std::as_bytes(bytes), storage_.bytes().subspan(used_).begin());
        used_ += bytes.size();
        return true;
    } catch (...) {
        exhausted_ = true;
        return false;
    }
}
core::Result<EncodedPng> encode_png(PngImage image, core::Budget& budget,
                                    const core::Cancellation& cancellation) {
    PngMemoryOutput output{budget};
    auto written = write_png(output.stream(), image, budget, cancellation);
    if (output.exhausted()) {
        return core::failure(core::ErrorCode::resource,
                             "The encoded PNG exhausted its bounded allocation budget");
    }
    if (!written) {
        return std::unexpected(std::move(written.error()));
    }
    return std::move(output).finish();
}
} // namespace docenhance::codec
