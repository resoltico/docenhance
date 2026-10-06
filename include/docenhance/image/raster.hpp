// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace docenhance::image {
inline constexpr unsigned byte_bits = 8;
inline constexpr unsigned word_bits = 16;
inline constexpr std::size_t chromaticity_fields = 8;
inline constexpr std::size_t cicp_fields = 4;
inline constexpr unsigned rgba_channels = 4;
inline constexpr std::uint32_t byte_max = 255;
inline constexpr std::uint32_t word_max = 65535;
inline constexpr std::size_t profile_limit = std::size_t{4} * 1024 * 1024;
// Admitted decoded/output sample precision. Encoded low-bit-depth PNG declarations remain a
// separate source-domain fact; they expand before entering this representation.
class SampleDepth {
  public:
    constexpr SampleDepth() noexcept = default;
    [[nodiscard]] static constexpr SampleDepth byte() noexcept {
        return {};
    }
    [[nodiscard]] static constexpr SampleDepth word() noexcept {
        return SampleDepth{word_bits};
    }
    [[nodiscard]] static constexpr std::optional<SampleDepth> from_bits(unsigned bits) noexcept {
        if (bits == byte_bits) {
            return byte();
        }
        if (bits == word_bits) {
            return word();
        }
        return std::nullopt;
    }
    [[nodiscard]] constexpr unsigned bits() const noexcept {
        return bits_;
    }
    [[nodiscard]] constexpr unsigned bytes() const noexcept {
        return bits_ / byte_bits;
    }
    constexpr auto operator<=>(const SampleDepth&) const = default;

  private:
    explicit constexpr SampleDepth(unsigned bits) noexcept : bits_(bits) {}
    unsigned bits_ = byte_bits;
};
// One admitted EXIF/wire orientation, including the absent-metadata identity default.
class Orientation {
  public:
    constexpr Orientation() noexcept = default;
    [[nodiscard]] static constexpr Orientation normal() noexcept {
        return {};
    }
    [[nodiscard]] static constexpr std::optional<Orientation> from_code(unsigned code) noexcept {
        if (code == 0 || code > static_cast<unsigned>(Code::rotate_left)) {
            return std::nullopt;
        }
        return Orientation{static_cast<Code>(code)};
    }
    [[nodiscard]] constexpr unsigned code() const noexcept {
        return static_cast<unsigned>(code_);
    }
    [[nodiscard]] constexpr bool transposed() const noexcept {
        return code_ >= Code::transpose;
    }
    [[nodiscard]] constexpr bool mirrored_x() const noexcept {
        return code_ == Code::mirror_horizontal || code_ == Code::rotate_half ||
               code_ == Code::transverse || code_ == Code::rotate_left;
    }
    [[nodiscard]] constexpr bool mirrored_y() const noexcept {
        return code_ == Code::rotate_half || code_ == Code::mirror_vertical ||
               code_ == Code::rotate_right || code_ == Code::transverse;
    }
    constexpr bool operator==(const Orientation&) const = default;

  private:
    enum class Code : unsigned {
        normal = 1,
        mirror_horizontal = 2,
        rotate_half = 3,
        mirror_vertical = 4,
        transpose = 5,
        rotate_right = 6,
        transverse = 7,
        rotate_left = 8,
    };
    explicit constexpr Orientation(Code code) noexcept : code_(code) {}
    Code code_ = Code::normal;
};
enum class SampleModel { gray, gray_alpha, rgb, rgba };
struct RasterShape {
    std::uint32_t width{};
    std::uint32_t height{};
    SampleModel model = SampleModel::gray;
    SampleDepth depth = SampleDepth::byte();
    bool operator==(const RasterShape&) const = default;
};
[[nodiscard]] unsigned components(SampleModel model) noexcept;
[[nodiscard]] bool is_color(SampleModel model) noexcept;
[[nodiscard]] bool has_alpha(SampleModel model) noexcept;
[[nodiscard]] core::Result<std::uint32_t> raster_row_bytes(RasterShape shape);
struct Resolution {
    std::uint32_t x{};
    std::uint32_t y{};
    bool operator==(const Resolution&) const = default;
};
// Common interpretation is independent of the container's declarations.
enum class JpegColor { gray, rgb, ycbcr };
struct JpegDeclarations {
    JpegColor color = JpegColor::gray;
};
struct TiffDeclarations {
    bool associated_alpha = false;
    bool miniswhite = false;
};
struct PngDeclarations {
    std::optional<std::uint32_t> gamma;
    std::optional<std::array<std::uint32_t, chromaticity_fields>> chromaticities;
    std::optional<unsigned> srgb;
    std::optional<std::array<std::uint8_t, cicp_fields>> cicp;
};
struct RasterMetadata {
    core::Buffer icc;
    Orientation orientation = Orientation::normal();
    std::optional<Resolution> resolution;
    std::variant<PngDeclarations, JpegDeclarations, TiffDeclarations> declarations;
    [[nodiscard]] PngDeclarations* png() & noexcept {
        return std::get_if<PngDeclarations>(&declarations);
    }
    [[nodiscard]] const PngDeclarations* png() const& noexcept {
        return std::get_if<PngDeclarations>(&declarations);
    }
    [[nodiscard]] PngDeclarations* png() && = delete;
    [[nodiscard]] const PngDeclarations* png() const&& = delete;
};
struct Raster {
    RasterShape shape;
    Plane<std::uint8_t> pixels;
    RasterMetadata metadata;
};
struct Coordinate {
    std::uint32_t x{};
    std::uint32_t y{};
};
[[nodiscard]] RasterShape oriented_shape(RasterShape shape, Orientation orientation) noexcept;
// The orientation is already admitted; the destination must be inside oriented_shape.
[[nodiscard]] Coordinate source_coordinate(RasterShape shape, Orientation orientation,
                                           Coordinate p) noexcept;
[[nodiscard]] std::uint16_t read_sample(std::span<const std::uint8_t> bytes,
                                        SampleDepth depth) noexcept;
void write_sample(std::span<std::uint8_t> bytes, SampleDepth depth, std::uint16_t sample) noexcept;
} // namespace docenhance::image
