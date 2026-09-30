// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "jpeg_scan.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/jpeg.hpp"
#include "jpeg_markers.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
namespace docenhance::io {
unsigned jpeg_word(std::span<const std::uint8_t> bytes) noexcept {
    return (static_cast<unsigned>(bytes.front()) << image::byte_bits) | bytes.subspan(1, 1).front();
}
namespace {
constexpr unsigned frame_fields = 6;
constexpr std::size_t component_descriptor_bytes = 3;
constexpr unsigned component_count_offset = 5;
constexpr unsigned width_offset = 3;
core::Result<void> frame(JpegMarkers& state, unsigned marker, std::span<const std::uint8_t> bytes,
                         JpegLimits limits) {
    if (state.framed || bytes.size() < frame_fields) {
        return core::failure(core::ErrorCode::input, "JPEG requires one complete image frame");
    }
    const auto components = bytes.subspan(component_count_offset, 1).front();
    if (bytes.front() != image::byte_bits ||
        (components != 1 && components != image::jpeg_components)) {
        return core::failure(core::ErrorCode::unavailable,
                             "JPEG requires 8-bit grayscale or three-component color");
    }
    if (bytes.size() != frame_fields + (std::size_t{components} * component_descriptor_bytes)) {
        return core::failure(core::ErrorCode::input, "Invalid JPEG frame extent");
    }
    state.framed = true;
    state.components = components;
    auto& source = state.scan.source;
    source.height = jpeg_word(bytes.subspan(1));
    source.width = jpeg_word(bytes.subspan(width_offset));
    source.process = marker == jpeg_sof_baseline ? image::JpegProcess::baseline
                                                 : image::JpegProcess::progressive;
    if (source.width == 0 || source.height == 0 || source.width > image::jpeg_dimension_max ||
        source.height > image::jpeg_dimension_max) {
        return core::failure(core::ErrorCode::input, "JPEG dimensions must be positive");
    }
    if (std::uint64_t{source.width} * source.height > limits.pixels) {
        return core::failure(core::ErrorCode::resource, "JPEG exceeds its pixel ceiling");
    }
    for (unsigned i = 0; i < components; ++i) {
        const auto component =
            bytes.subspan(frame_fields + (std::size_t{i} * component_descriptor_bytes),
                          component_descriptor_bytes);
        state.scan.source.component_ids.at(i) = component.front();
        for (unsigned earlier = 0; earlier < i; ++earlier) {
            if (state.scan.source.component_ids.at(earlier) == component.front()) {
                return core::failure(core::ErrorCode::input, "Duplicate JPEG component identity");
            }
        }
        const unsigned sampling = component.subspan(1, 1).front();
        const unsigned h = sampling >> jpeg_sampling_shift;
        const unsigned v = sampling & jpeg_sampling_mask;
        if (h == 0 || h > image::jpeg_sampling_max || v == 0 || v > image::jpeg_sampling_max ||
            component.back() > image::jpeg_components) {
            return core::failure(core::ErrorCode::input,
                                 "Invalid JPEG sampling or quantization selector");
        }
        source.sampling.at(i) = {.horizontal = h, .vertical = v};
    }
    return {};
}
core::Result<void> entropy(std::span<const std::uint8_t>& bytes,
                           const core::Cancellation& cancellation) {
    std::size_t index = 0;
    constexpr std::size_t checkpoint_bytes = std::size_t{64} * 1024;
    while (index < bytes.size()) {
        if (index % checkpoint_bytes == 0 && cancellation.requested(core::Checkpoint::decode)) {
            return core::cancelled();
        }
        if (bytes.subspan(index, 1).front() != jpeg_marker_prefix) {
            ++index;
            continue;
        }
        const auto begin = index;
        while (index < bytes.size() && bytes.subspan(index, 1).front() == jpeg_marker_prefix) {
            if (index % checkpoint_bytes == 0 && cancellation.requested(core::Checkpoint::decode)) {
                return core::cancelled();
            }
            ++index;
        }
        if (index == bytes.size()) {
            break;
        }
        const auto marker = bytes.subspan(index, 1).front();
        if (marker == 0 || (marker >= jpeg_restart_first && marker <= jpeg_restart_last)) {
            ++index;
            continue;
        }
        bytes = bytes.subspan(begin);
        return {};
    }
    return core::failure(core::ErrorCode::input, "JPEG entropy stream has no closing marker");
}
core::Result<unsigned> take_marker(std::span<const std::uint8_t>& bytes,
                                   const core::Cancellation& cancellation) {
    if (bytes.empty() || bytes.front() != jpeg_marker_prefix) {
        return core::failure(core::ErrorCode::input, "Invalid JPEG marker boundary");
    }
    // Marker fill has a fixed input ceiling; cancellation was observed by the outer loop.
    std::size_t count = 0;
    while (count < bytes.size() && bytes.subspan(count, 1).front() == jpeg_marker_prefix) {
        constexpr std::size_t checkpoint_bytes = std::size_t{64} * 1024;
        if (count % checkpoint_bytes == 0 && cancellation.requested(core::Checkpoint::decode)) {
            return core::cancelled();
        }
        ++count;
    }
    if (count == bytes.size()) {
        return core::failure(core::ErrorCode::input, "Truncated JPEG marker");
    }
    const unsigned marker = bytes.subspan(count, 1).front();
    bytes = bytes.subspan(count + 1);
    return marker;
}
} // namespace
namespace {
struct Segment {
    unsigned marker{};
    std::span<const std::uint8_t> payload;
};
bool application_marker(unsigned marker) noexcept {
    return (marker >= jpeg_app_first && marker <= jpeg_app_last) || marker == jpeg_comment;
}
core::Result<void> input_limits(std::span<const std::uint8_t> bytes, JpegLimits limits) {
    if (limits.encoded_bytes == 0 || limits.encoded_bytes > jpeg_max_encoded_bytes ||
        limits.pixels == 0 || limits.pixels > jpeg_max_pixels || limits.scans == 0 ||
        limits.scans > jpeg_max_scans) {
        return core::failure(core::ErrorCode::argument,
                             "JPEG limits must be positive and within production ceilings");
    }
    if (bytes.size() > limits.encoded_bytes) {
        return core::failure(core::ErrorCode::resource, "JPEG exceeds its encoded-byte ceiling");
    }
    if (bytes.size() < 2 || bytes.front() != jpeg_marker_prefix ||
        bytes.subspan(1, 1).front() != jpeg_soi) {
        return core::failure(core::ErrorCode::input, "Invalid JPEG signature");
    }
    return {};
}
core::Result<Segment> take_segment(std::span<const std::uint8_t>& bytes,
                                   const core::Cancellation& cancellation) {
    auto marker = take_marker(bytes, cancellation);
    if (!marker) {
        return std::unexpected(marker.error());
    }
    if (*marker == jpeg_eoi) {
        return Segment{.marker = *marker, .payload = {}};
    }
    constexpr auto coding = std::to_array<unsigned>(
        {jpeg_sof_baseline, jpeg_sof_progressive, jpeg_dht, jpeg_dqt, jpeg_dri, jpeg_sos});
    if (!application_marker(*marker) && std::ranges::find(coding, *marker) == coding.end()) {
        return core::failure(core::ErrorCode::unavailable,
                             "Unsupported JPEG coding process or marker");
    }
    if (bytes.size() < 2) {
        return core::failure(core::ErrorCode::input, "Truncated JPEG marker length");
    }
    const auto size = jpeg_word(bytes);
    if (size < 2 || size > bytes.size()) {
        return core::failure(core::ErrorCode::input, "JPEG marker exceeds encoded input");
    }
    const auto payload = bytes.subspan(2, size - 2);
    bytes = bytes.subspan(size);
    return Segment{.marker = *marker, .payload = payload};
}
core::Result<void> consume_segment(JpegMarkers& state, Segment segment,
                                   std::span<const std::uint8_t>& bytes,
                                   const core::Cancellation& cancellation, JpegLimits limits) {
    if (segment.marker == jpeg_sof_baseline || segment.marker == jpeg_sof_progressive) {
        return frame(state, segment.marker, segment.payload, limits);
    }
    if (application_marker(segment.marker)) {
        return jpeg_metadata(state, segment.marker, segment.payload);
    }
    if (segment.marker != jpeg_sos) {
        return {};
    }
    if (!state.framed) {
        return core::failure(core::ErrorCode::input, "JPEG scan precedes its frame");
    }
    if (++state.scan.source.scans > limits.scans) {
        return core::failure(core::ErrorCode::resource, "JPEG exceeds its scan ceiling");
    }
    return entropy(bytes, cancellation);
}
core::Result<JpegScan> finish(JpegMarkers& state, std::span<const std::uint8_t> bytes,
                              core::Budget& budget, image::ProfilePolicy policy,
                              const core::Cancellation& cancellation) {
    if (!state.framed || state.scan.source.scans == 0 || !bytes.empty()) {
        return core::failure(core::ErrorCode::input,
                             "JPEG needs a complete single image without trailing bytes");
    }
    auto finished = finish_jpeg_metadata(state, budget, policy, cancellation);
    if (!finished) {
        return std::unexpected(finished.error());
    }
    return std::move(state.scan);
}
} // namespace
core::Result<JpegScan> scan_jpeg(std::span<const std::uint8_t> bytes, core::Budget& budget,
                                 image::ProfilePolicy policy,
                                 const core::Cancellation& cancellation, JpegLimits limits) {
    auto valid = input_limits(bytes, limits);
    if (!valid) {
        return std::unexpected(valid.error());
    }
    bytes = bytes.subspan(2);
    JpegMarkers state;
    for (std::size_t count = 0; count < jpeg_max_markers; ++count) {
        if (cancellation.requested(core::Checkpoint::decode)) {
            return core::cancelled();
        }
        auto segment = take_segment(bytes, cancellation);
        if (!segment) {
            return std::unexpected(segment.error());
        }
        if (segment->marker == jpeg_eoi) {
            return finish(state, bytes, budget, policy, cancellation);
        }
        auto consumed = consume_segment(state, *segment, bytes, cancellation, limits);
        if (!consumed) {
            return std::unexpected(consumed.error());
        }
    }
    return core::failure(core::ErrorCode::resource, "JPEG exceeds its marker-count ceiling");
}
} // namespace docenhance::io
