// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "tiff_ifd.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/tiff.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
namespace docenhance::io {
namespace {
constexpr unsigned classic_magic = 42;
constexpr unsigned big_magic = 43;
constexpr unsigned classic_header_bytes = 8;
constexpr unsigned big_header_bytes = 16;
constexpr unsigned classic_entry_bytes = 12;
constexpr unsigned big_entry_bytes = 20;
constexpr unsigned short_type = 3;
constexpr unsigned long_type = 4;
constexpr unsigned long8_type = 16;
constexpr unsigned field_header_bytes = 4;
constexpr unsigned short_bytes = sizeof(std::uint16_t);
constexpr unsigned long_bytes = sizeof(std::uint32_t);
constexpr unsigned long8_bytes = sizeof(std::uint64_t);
unsigned type_size(unsigned type) noexcept {
    constexpr std::array<unsigned, 19> sizes{
        // Classic field types; zero is invalid and types 14/15 are reserved.
        0,
        1,
        1,
        2,
        4,
        8,
        1,
        1,
        2,
        4,
        8,
        4,
        8,
        4,
        0,
        0,
        // BigTIFF LONG8, SLONG8 and IFD8 fields.
        8,
        8,
        8,
    };
    return type < sizes.size() ? std::span{sizes}.subspan(type, 1).front() : 0;
}
bool signature(std::span<const std::uint8_t> bytes) noexcept {
    return bytes.size() >= classic_header_bytes &&
           bytes.size() <= image::source_encoded_bytes_max && has_tiff_signature(bytes);
}
bool big_header(const TiffIfd& ifd) noexcept {
    return ifd.bytes.size() >= big_header_bytes &&
           ifd.number(ifd.bytes.subspan(long_bytes, short_bytes)) == long8_bytes &&
           ifd.number(ifd.bytes.subspan(long_bytes + short_bytes, short_bytes)) == 0;
}
core::Result<TiffIfd> invalid() {
    return core::failure(core::ErrorCode::input, "TIFF directory is malformed or unsupported");
}
std::optional<TiffField> entry(const TiffIfd& ifd, std::span<const std::uint8_t> bytes) noexcept {
    const auto type = static_cast<unsigned>(ifd.number(bytes.subspan(2, 2)));
    const auto count_size = ifd.big ? long8_bytes : long_bytes;
    const auto count = ifd.number(bytes.subspan(field_header_bytes, count_size));
    const auto size = type_size(type);
    if ((!ifd.big && type >= long8_type) || size == 0 || count > ifd.bytes.size() / size) {
        return std::nullopt;
    }
    const auto extent = static_cast<std::size_t>(count) * size;
    const auto value = bytes.subspan(field_header_bytes + count_size, count_size);
    if (extent <= value.size()) {
        return TiffField{.type = type, .count = count, .bytes = value.first(extent)};
    }
    const auto offset = ifd.number(value);
    if (offset > ifd.bytes.size() || extent > ifd.bytes.size() - offset) {
        return std::nullopt;
    }
    return TiffField{
        .type = type,
        .count = count,
        .bytes = ifd.bytes.subspan(static_cast<std::size_t>(offset), extent),
    };
}
core::Result<void> valid_entries(const TiffIfd& ifd, const core::Cancellation& cancellation) {
    unsigned previous = 0;
    for (unsigned i = 0; i < ifd.entries; ++i) {
        if (cancellation.requested(core::Checkpoint::decode)) {
            return std::unexpected(core::cancelled().error());
        }
        const auto bytes_entry =
            ifd.bytes.subspan(ifd.start + (std::size_t{i} * ifd.entry_size()), ifd.entry_size());
        const auto tag = static_cast<unsigned>(ifd.number(bytes_entry.first(2)));
        // TIFF requires sorted unique tags; no repair of duplicated or reordered fields.
        if (tag <= previous || !entry(ifd, bytes_entry)) {
            return core::failure(core::ErrorCode::input,
                                 "TIFF entries are duplicated, unordered or malformed");
        }
        previous = tag;
    }
    return {};
}
} // namespace
bool has_tiff_signature(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() < long_bytes || bytes.front() != bytes.subspan(1, 1).front() ||
        (bytes.front() != 'I' && bytes.front() != 'M')) {
        return false;
    }
    const TiffIfd ifd{.bytes = bytes, .little = bytes.front() == 'I'};
    const auto magic = ifd.number(bytes.subspan(short_bytes, short_bytes));
    return magic == classic_magic || magic == big_magic;
}
unsigned TiffIfd::entry_size() const noexcept {
    return big ? big_entry_bytes : classic_entry_bytes;
}
std::uint64_t TiffIfd::number(std::span<const std::uint8_t> value) const noexcept {
    std::uint64_t result = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto index = little ? value.size() - 1 - i : i;
        result = (result << image::byte_bits) | value.subspan(index, 1).front();
    }
    return result;
}
std::optional<TiffField> TiffIfd::field(unsigned tag) const noexcept {
    for (unsigned i = 0; i < entries; ++i) {
        const auto bytes_entry =
            bytes.subspan(start + (std::size_t{i} * entry_size()), entry_size());
        if (number(bytes_entry.first(2)) == tag) {
            return entry(*this, bytes_entry);
        }
    }
    return std::nullopt;
}
std::optional<std::uint64_t> TiffIfd::integer(unsigned tag, std::uint64_t fallback) const noexcept {
    const auto value = field(tag);
    if (!value) {
        return fallback;
    }
    if (value->count != 1 || (value->type != 1 && value->type != short_type &&
                              value->type != long_type && value->type != long8_type)) {
        return std::nullopt;
    }
    return number(value->bytes);
}
core::Result<TiffIfd> scan_tiff(std::span<const std::uint8_t> bytes,
                                const core::Cancellation& cancellation) {
    if (!signature(bytes)) {
        return invalid();
    }
    TiffIfd ifd{.bytes = bytes, .little = bytes.front() == 'I'};
    const auto magic = ifd.number(bytes.subspan(2, 2));
    ifd.big = magic == big_magic;
    if (magic != classic_magic && magic != big_magic) {
        return invalid();
    }
    const unsigned offset_size = ifd.big ? long8_bytes : long_bytes;
    const unsigned count_size = ifd.big ? long8_bytes : short_bytes;
    if (ifd.big && !big_header(ifd)) {
        return invalid();
    }
    const auto offset = ifd.number(bytes.subspan(ifd.big ? long8_bytes : long_bytes, offset_size));
    if (offset < (ifd.big ? big_header_bytes : classic_header_bytes) || offset > bytes.size() ||
        count_size > bytes.size() - offset) {
        return invalid();
    }
    const auto count = ifd.number(bytes.subspan(static_cast<std::size_t>(offset), count_size));
    if (count == 0) {
        return invalid();
    }
    if (count > tiff_entries_max) {
        return core::failure(core::ErrorCode::resource, "TIFF directory entry limit exceeded");
    }
    ifd.entries = static_cast<unsigned>(count);
    ifd.start = static_cast<std::size_t>(offset) + count_size;
    const auto extent = std::size_t{ifd.entries} * ifd.entry_size();
    if (extent > bytes.size() - ifd.start || offset_size > bytes.size() - ifd.start - extent) {
        return invalid();
    }
    if (ifd.number(bytes.subspan(ifd.start + extent, offset_size)) != 0) {
        return core::failure(core::ErrorCode::input, "Multipage TIFF input is not supported");
    }
    const auto checked = valid_entries(ifd, cancellation);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    return ifd;
}
} // namespace docenhance::io
