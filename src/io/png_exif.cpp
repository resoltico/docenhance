// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "png_metadata.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>

namespace docenhance::io {
namespace {
constexpr std::size_t exif_limit = std::size_t{64} * 1024;
constexpr std::size_t entry_bytes = 12;
constexpr std::size_t max_entries = 128;
constexpr std::size_t tiff_header = 8;
constexpr unsigned short_type = 3;
constexpr unsigned rational_type = 5;
constexpr unsigned orientation_tag = 0x112;
constexpr unsigned resolution_x_tag = 0x11a;
constexpr unsigned resolution_y_tag = 0x11b;
constexpr unsigned resolution_unit_tag = 0x128;
struct Exif {
    std::span<const std::uint8_t> bytes;
    bool little;
    [[nodiscard]] std::uint32_t number(std::size_t offset, std::size_t count) const noexcept {
        std::uint32_t result = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto index = little ? count - 1 - i : i;
            result = (result << image::byte_bits) | bytes.subspan(offset + index, 1).front();
        }
        return result;
    }
    [[nodiscard]] core::Result<double> rational(std::size_t offset) const {
        constexpr std::size_t size = 8;
        if (offset > bytes.size() || bytes.size() - offset < size) {
            return core::failure(core::ErrorCode::input, "EXIF rational is outside the chunk");
        }
        const auto denominator = number(offset + png_integer_bytes, png_integer_bytes);
        if (denominator == 0) {
            return core::failure(core::ErrorCode::input, "EXIF resolution has a zero denominator");
        }
        return static_cast<double>(number(offset, png_integer_bytes)) / denominator;
    }
};
struct ExifFields {
    std::optional<unsigned> orientation;
    std::optional<unsigned> unit;
    std::optional<double> x;
    std::optional<double> y;
};
core::Result<void> read_entry(const Exif& exif, std::size_t at, ExifFields& fields) {
    const auto tag = exif.number(at, 2);
    const bool short_field = tag == orientation_tag || tag == resolution_unit_tag;
    const bool rational_field = tag == resolution_x_tag || tag == resolution_y_tag;
    if (!short_field && !rational_field) {
        return {};
    }
    const auto type = exif.number(at + 2, 2);
    const auto count = exif.number(at + png_integer_bytes, png_integer_bytes);
    constexpr std::size_t value_offset = 8;
    if (count != 1 || type != (short_field ? short_type : rational_type)) {
        return core::failure(core::ErrorCode::input,
                             "EXIF orientation/resolution has invalid type");
    }
    if (short_field) {
        auto& field = tag == orientation_tag ? fields.orientation : fields.unit;
        if (field) {
            return core::failure(core::ErrorCode::input, "Duplicate EXIF field");
        }
        field = exif.number(at + value_offset, 2);
    } else {
        auto& field = tag == resolution_x_tag ? fields.x : fields.y;
        if (field) {
            return core::failure(core::ErrorCode::input, "Duplicate EXIF resolution");
        }
        auto value = exif.rational(exif.number(at + value_offset, png_integer_bytes));
        if (!value) {
            return std::unexpected(value.error());
        }
        field = *value;
    }
    return {};
}
core::Result<void> assign_fields(const ExifFields& fields, image::PngMetadata& metadata) {
    constexpr unsigned orientation_max = 8;
    metadata.orientation = fields.orientation.value_or(1);
    if (metadata.orientation < 1 || metadata.orientation > orientation_max ||
        (fields.unit && (*fields.unit < 1 || *fields.unit > short_type))) {
        return core::failure(core::ErrorCode::input,
                             "EXIF orientation or resolution unit is invalid");
    }
    if (metadata.resolution || !fields.x || !fields.y || fields.unit.value_or(2) == 1) {
        return {};
    }
    constexpr double inches_per_meter = 100.0 / 2.54;
    constexpr double centimeters_per_meter = 100.0;
    const auto multiplier = fields.unit.value_or(2) == 2 ? inches_per_meter : centimeters_per_meter;
    const double x = std::floor((*fields.x * multiplier) + 0.5);
    const double y = std::floor((*fields.y * multiplier) + 0.5);
    if (x < 1 || y < 1 || x > std::numeric_limits<std::uint32_t>::max() ||
        y > std::numeric_limits<std::uint32_t>::max()) {
        return core::failure(core::ErrorCode::input,
                             "EXIF physical resolution is outside its range");
    }
    metadata.resolution =
        image::Resolution{.x = static_cast<std::uint32_t>(x), .y = static_cast<std::uint32_t>(y)};
    return {};
}
} // namespace
core::Result<void> parse_exif(std::span<const std::uint8_t> bytes, image::PngMetadata& metadata) {
    if (bytes.size() < tiff_header || bytes.size() > exif_limit ||
        ((bytes.subspan(0, 1).front() != 'I' || bytes.subspan(1, 1).front() != 'I') &&
         (bytes.subspan(0, 1).front() != 'M' || bytes.subspan(1, 1).front() != 'M'))) {
        return core::failure(core::ErrorCode::input, "Invalid PNG EXIF header");
    }
    const Exif exif{.bytes = bytes, .little = bytes.subspan(0, 1).front() == 'I'};
    constexpr unsigned tiff_magic = 42;
    const auto offset = exif.number(png_integer_bytes, png_integer_bytes);
    if (exif.number(2, 2) != tiff_magic || offset < tiff_header || offset > bytes.size() - 2) {
        return core::failure(core::ErrorCode::input, "Invalid PNG EXIF IFD offset");
    }
    const auto count = exif.number(offset, 2);
    const auto table = std::size_t{offset} + 2;
    if (count > max_entries || bytes.size() - table < (count * entry_bytes) + png_integer_bytes) {
        return core::failure(core::ErrorCode::input, "PNG EXIF IFD exceeds its bounds");
    }
    ExifFields fields;
    for (std::size_t i = 0; i < count; ++i) {
        auto result = read_entry(exif, table + (i * entry_bytes), fields);
        if (!result) {
            return result;
        }
    }
    // Only IFD0 is interpreted. GPS, MakerNote, thumbnail and secondary IFDs are not traversed.
    return assign_fields(fields, metadata);
}
} // namespace docenhance::io
