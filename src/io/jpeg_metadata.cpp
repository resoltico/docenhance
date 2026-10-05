// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "exif.hpp"
#include "jpeg_markers.hpp"
#include "jpeg_scan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <utility>
namespace docenhance::io {
namespace {
constexpr std::size_t mpf_signature_bytes = 4;
constexpr unsigned jfif_thumbnail_channels = 3;
constexpr std::size_t jfif_header_bytes = 14;
constexpr std::size_t jfif_major_offset = 5;
constexpr std::size_t jfif_minor_offset = 6;
constexpr std::size_t jfif_unit_offset = 7;
constexpr std::size_t jfif_x_offset = 8;
constexpr std::size_t jfif_y_offset = 10;
constexpr std::size_t jfif_thumbnail_x = 12;
constexpr std::size_t jfif_thumbnail_y = 13;
constexpr std::size_t exif_signature_bytes = 6;
constexpr std::size_t icc_signature_bytes = 12;
constexpr std::size_t adobe_marker_bytes = 12;
constexpr double rounding_half = 0.5;
constexpr double centimeters_per_meter = 100.0;
constexpr double centimeters_per_inch = 2.54;

bool starts(std::span<const std::uint8_t> bytes, std::string_view prefix) noexcept {
    return bytes.size() >= prefix.size() && std::ranges::equal(bytes.first(prefix.size()), prefix);
}
bool valid_signature(unsigned marker, std::span<const std::uint8_t> bytes) noexcept {
    const bool invalid_exif = marker == jpeg_app_exif && starts(bytes, "Exif") &&
                              !starts(bytes, std::string_view{"Exif\0\0", exif_signature_bytes});
    const bool invalid_icc = marker == jpeg_app_icc && starts(bytes, "ICC_PROFILE") &&
                             !starts(bytes, std::string_view{"ICC_PROFILE\0", icc_signature_bytes});
    const bool invalid_jfif = marker == jpeg_app_first && starts(bytes, "JFIF") &&
                              !starts(bytes, std::string_view{"JFIF\0", jfif_major_offset});
    return !invalid_exif && !invalid_icc && !invalid_jfif;
}
bool contains(std::span<const std::uint8_t> bytes, std::string_view text) noexcept {
    return !std::ranges::search(bytes, text).empty();
}
bool unsupported_extension(unsigned marker, std::span<const std::uint8_t> bytes) noexcept {
    if (marker == jpeg_app_jumbf) {
        return true;
    }
    if (marker == jpeg_app_icc) {
        return starts(bytes, std::string_view{"MPF\0", mpf_signature_bytes}) ||
               starts(bytes, "urn:iso:std:iso:ts:21496");
    }
    if (marker != jpeg_app_exif) {
        return false;
    }
    if (starts(bytes, "http://ns.adobe.com/xmp/extension") ||
        starts(bytes, "http://ns.adobe.com/hdr-gain-map")) {
        return true;
    }
    if (!starts(bytes, "http://ns.adobe.com/xap/1.0/")) {
        return false;
    }
    return contains(bytes, "hdr-gain-map") || contains(bytes, "hdrgm:") ||
           contains(bytes, "Container:Directory");
}
core::Result<void> jfif(JpegMarkers& state, std::span<const std::uint8_t> bytes) {
    if (state.scan.source.jfif_present || bytes.size() < jfif_header_bytes ||
        bytes.subspan(jfif_major_offset, 1).front() != 1 ||
        bytes.subspan(jfif_minor_offset, 1).front() > 2 ||
        bytes.subspan(jfif_unit_offset, 1).front() > 2) {
        return core::failure(core::ErrorCode::input, "Invalid or duplicate JPEG JFIF declaration");
    }
    state.scan.source.jfif_present = true;
    const auto unit = bytes.subspan(jfif_unit_offset, 1).front();
    const auto x = jpeg_word(bytes.subspan(jfif_x_offset));
    const auto y = jpeg_word(bytes.subspan(jfif_y_offset));
    const auto thumbnail = std::size_t{bytes.subspan(jfif_thumbnail_x, 1).front()} *
                           bytes.subspan(jfif_thumbnail_y, 1).front() * jfif_thumbnail_channels;
    if (bytes.size() != jfif_header_bytes + thumbnail || x == 0 || y == 0) {
        return core::failure(core::ErrorCode::input,
                             "Invalid JPEG JFIF density or thumbnail extent");
    }
    if (unit != 0) {
        const auto scale =
            unit == 1 ? centimeters_per_meter / centimeters_per_inch : centimeters_per_meter;
        state.scan.source.jfif_resolution = image::Resolution{
            .x = static_cast<std::uint32_t>(std::floor((x * scale) + rounding_half)),
            .y = static_cast<std::uint32_t>(std::floor((y * scale) + rounding_half)),
        };
    }
    return {};
}
core::Result<void> icc(JpegMarkers& state, std::span<const std::uint8_t> bytes) {
    constexpr std::size_t prefix = 12;
    if (bytes.size() <= prefix + 2) {
        return core::failure(core::ErrorCode::input, "Truncated JPEG ICC segment");
    }
    const auto sequence = bytes.subspan(prefix, 1).front();
    const auto count = bytes.subspan(prefix + 1, 1).front();
    if (sequence == 0 || count == 0 || sequence > count ||
        (state.icc_count != 0 && state.icc_count != count) || !state.icc.at(sequence - 1).empty()) {
        return core::failure(core::ErrorCode::input,
                             "Invalid, duplicate or conflicting JPEG ICC sequence");
    }
    state.icc_count = count;
    state.icc.at(sequence - 1) = bytes.subspan(prefix + 2);
    if (state.icc.at(sequence - 1).size() > image::profile_limit - state.profile_bytes) {
        return core::failure(core::ErrorCode::resource,
                             "JPEG ICC profile exceeds its byte ceiling");
    }
    state.profile_bytes += state.icc.at(sequence - 1).size();
    return {};
}
core::Result<void> sampling_contract(const JpegMarkers& state) {
    const auto& source = state.scan.source;
    const auto first = source.sampling.front();
    for (unsigned i = 0; i < state.components; ++i) {
        const auto sampling = source.sampling.at(i);
        if ((source.color != image::JpegColor::ycbcr || i != 0) &&
            (sampling.horizontal != 1 || sampling.vertical != 1)) {
            return core::failure(core::ErrorCode::input, "Unsupported JPEG sampling layout");
        }
    }
    if ((first.horizontal * first.vertical) + (state.components - 1) > image::jpeg_mcu_blocks_max) {
        return core::failure(core::ErrorCode::input,
                             "JPEG sampling exceeds the admitted MCU extent");
    }
    return {};
}
core::Result<void> choose_color(JpegMarkers& state) {
    auto& source = state.scan.source;
    if (state.components == 1) {
        source.color = image::JpegColor::gray;
        if (source.adobe_transform && *source.adobe_transform != 0) {
            return core::failure(core::ErrorCode::input,
                                 "JPEG gray components conflict with Adobe transform");
        }
    } else {
        const bool rgb_ids = state.scan.source.component_ids ==
                             std::array<unsigned, image::jpeg_components>{'R', 'G', 'B'};
        const bool ycc_ids =
            state.scan.source.component_ids == std::array<unsigned, image::jpeg_components>{
                                                   1,
                                                   2,
                                                   image::jpeg_components,
                                               };
        if (source.jfif_present && source.adobe_transform == 0) {
            return core::failure(core::ErrorCode::input,
                                 "JPEG JFIF and Adobe color declarations conflict");
        }
        if (source.adobe_transform) {
            source.color =
                *source.adobe_transform == 0 ? image::JpegColor::rgb : image::JpegColor::ycbcr;
        } else if (source.jfif_present || ycc_ids) {
            source.color = image::JpegColor::ycbcr;
        } else if (rgb_ids) {
            source.color = image::JpegColor::rgb;
        } else {
            return core::failure(core::ErrorCode::input,
                                 "JPEG component interpretation is ambiguous");
        }
    }
    const auto valid = sampling_contract(state);
    if (!valid) {
        return valid;
    }
    state.scan.metadata.declarations = image::JpegDeclarations{.color = source.color};
    return {};
}
} // namespace
namespace {
enum class Declaration { ignored, jfif, exif, icc, adobe };
Declaration declaration(unsigned marker, std::span<const std::uint8_t> bytes) noexcept {
    if (marker == jpeg_app_first && starts(bytes, "JFIF")) {
        return Declaration::jfif;
    }
    if (marker == jpeg_app_exif && starts(bytes, "Exif")) {
        return Declaration::exif;
    }
    if (marker == jpeg_app_icc && starts(bytes, "ICC_PROFILE")) {
        return Declaration::icc;
    }
    if (marker == jpeg_app_adobe && starts(bytes, "Adobe")) {
        return Declaration::adobe;
    }
    return Declaration::ignored;
}
core::Result<void> retain(JpegMarkers& state, Declaration kind,
                          std::span<const std::uint8_t> bytes) {
    switch (kind) {
    case Declaration::jfif:
        return jfif(state, bytes);
    case Declaration::icc:
        return icc(state, bytes);
    case Declaration::exif:
        if (state.scan.source.exif_present) {
            return core::failure(core::ErrorCode::input, "Duplicate JPEG EXIF declaration");
        }
        state.scan.source.exif_present = true;
        state.exif = bytes.subspan(exif_signature_bytes);
        break;
    case Declaration::adobe:
        if (state.scan.source.adobe_transform || bytes.size() != adobe_marker_bytes ||
            bytes.back() > 1) {
            return core::failure(core::ErrorCode::input,
                                 "Unsupported or conflicting JPEG Adobe declaration");
        }
        state.scan.source.adobe_transform = bytes.back();
        break;
    case Declaration::ignored:
        break;
    }
    return {};
}
} // namespace
core::Result<void> jpeg_metadata(JpegMarkers& state, unsigned marker,
                                 std::span<const std::uint8_t> bytes) {
    constexpr std::size_t metadata_limit = std::size_t{8} * 1024 * 1024;
    if (bytes.size() > metadata_limit - state.marker_bytes) {
        return core::failure(core::ErrorCode::resource,
                             "JPEG marker data exceeds its byte ceiling");
    }
    state.marker_bytes += bytes.size();
    if (!valid_signature(marker, bytes)) {
        return core::failure(core::ErrorCode::input, "Malformed JPEG metadata signature");
    }
    if (unsupported_extension(marker, bytes)) {
        return core::failure(core::ErrorCode::input,
                             "JPEG multi-image, gain-map or extended metadata is unsupported");
    }
    const auto kind = declaration(marker, bytes);
    if (kind != Declaration::ignored && state.scan.source.scans != 0) {
        return core::failure(core::ErrorCode::input,
                             "JPEG interpretation metadata must precede image scans");
    }
    return retain(state, kind, bytes);
}
core::Result<void> finish_jpeg_metadata(JpegMarkers& state, core::Budget& budget,
                                        image::ProfilePolicy policy,
                                        const core::Cancellation& cancellation) {
    auto color = choose_color(state);
    if (!color) {
        return color;
    }
    if (state.scan.source.exif_present) {
        auto parsed = parse_exif(state.exif, state.scan.metadata);
        if (!parsed) {
            return parsed;
        }
    }
    auto& source = state.scan.source;
    source.exif_resolution = state.scan.metadata.resolution;
    source.resolution_conflict = source.exif_resolution && source.jfif_resolution &&
                                 *source.exif_resolution != *source.jfif_resolution;
    if (!source.exif_resolution) {
        state.scan.metadata.resolution = source.jfif_resolution;
    }
    for (unsigned i = 0; i < state.icc_count; ++i) {
        if (state.icc.at(i).empty()) {
            return core::failure(core::ErrorCode::input, "JPEG ICC sequence is incomplete");
        }
    }
    if (state.icc_count != 0 && policy == image::ProfilePolicy::embedded) {
        auto profile = budget.allocate(state.profile_bytes);
        if (!profile) {
            return std::unexpected(profile.error());
        }
        auto destination = profile->bytes();
        for (unsigned i = 0; i < state.icc_count; ++i) {
            if (cancellation.requested(core::Checkpoint::decode)) {
                return core::cancelled();
            }
            const auto segment = state.icc.at(i);
            std::ranges::copy(std::as_bytes(segment), destination.begin());
            destination = destination.subspan(segment.size());
        }
        state.scan.metadata.icc = std::move(*profile);
    }
    return {};
}
} // namespace docenhance::io
