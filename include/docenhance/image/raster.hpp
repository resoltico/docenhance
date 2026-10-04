// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace docenhance::image {
inline constexpr unsigned byte_bits = 8;
inline constexpr unsigned orientation_max = 8;
inline constexpr unsigned word_bits = 16;
inline constexpr std::size_t chromaticity_fields = 8;
inline constexpr std::size_t cicp_fields = 4;
inline constexpr unsigned rgba_channels = 4;
inline constexpr std::uint32_t byte_max = 255;
inline constexpr std::uint32_t word_max = 65535;
inline constexpr std::size_t profile_limit = std::size_t{4} * 1024 * 1024;
enum class SampleModel { gray, gray_alpha, rgb, rgba };
struct RasterShape {
    std::uint32_t width{};
    std::uint32_t height{};
    SampleModel model = SampleModel::gray;
    unsigned depth = byte_bits;
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
struct PngDeclarations {
    std::optional<std::uint32_t> gamma;
    std::optional<std::array<std::uint32_t, chromaticity_fields>> chromaticities;
    std::optional<unsigned> srgb;
    std::optional<std::array<std::uint8_t, cicp_fields>> cicp;
};
struct RasterMetadata {
    core::Buffer icc;
    unsigned orientation = 1;
    std::optional<Resolution> resolution;
    std::variant<PngDeclarations, JpegDeclarations> declarations;
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
[[nodiscard]] RasterShape oriented_shape(RasterShape shape, unsigned orientation) noexcept;
// Precondition: orientation 1..8 and destination coordinate inside oriented_shape.
[[nodiscard]] Coordinate source_coordinate(RasterShape shape, unsigned orientation,
                                           Coordinate p) noexcept;
[[nodiscard]] std::uint16_t read_sample(std::span<const std::uint8_t> bytes,
                                        unsigned depth) noexcept;
void write_sample(std::span<std::uint8_t> bytes, unsigned depth, std::uint16_t sample) noexcept;
} // namespace docenhance::image
