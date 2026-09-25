// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace docenhance::image {
enum class ToneMode { preserve, gray };
enum class OutputDepth { automatic, byte, word };
enum class AlphaPolicy { white, black, reject };
enum class ProfilePolicy { embedded, srgb };
struct ToneParameters {
    ToneMode mode = ToneMode::preserve;
    OutputDepth depth = OutputDepth::automatic;
    AlphaPolicy alpha = AlphaPolicy::white;
    ProfilePolicy profile = ProfilePolicy::embedded;
};
class Continuous {
  public:
    [[nodiscard]] static core::Result<Continuous> create(ToneParameters values);
    [[nodiscard]] ToneParameters parameters() const noexcept {
        return parameters_;
    }

  private:
    explicit Continuous(ToneParameters parameters) : parameters_(parameters) {}
    ToneParameters parameters_;
};
enum class Interpretation { assumed_srgb, overridden_srgb, srgb, cicp, icc, gamma, chromaticities };
[[nodiscard]] std::string_view interpretation_name(Interpretation value) noexcept;
struct ConversionReport {
    RasterShape source;
    RasterShape output;
    Interpretation interpretation = Interpretation::assumed_srgb;
    unsigned orientation = 1;
    std::optional<Resolution> resolution;
    std::uint64_t flattened_pixels{};
    std::uint64_t clipped_components{};
    bool assumed_transfer = false;
    bool assumed_primaries = false;
    bool depth_reduced = false;
    bool verified = false;
};
struct OutputDescriptor {
    RasterShape shape;
    std::span<const std::uint8_t> profile;
    std::optional<Resolution> resolution;
};
enum class RowUse { output, verification, measurement };
// A borrowed row provider. Writers call it outside native jump frames; it never opens files.
class RowSource {
  public:
    RowSource() = default;
    RowSource(const RowSource&) = delete;
    RowSource& operator=(const RowSource&) = delete;
    RowSource(RowSource&&) = delete;
    RowSource& operator=(RowSource&&) = delete;
    virtual ~RowSource() = default;
    [[nodiscard]] virtual OutputDescriptor descriptor() const noexcept = 0;
    [[nodiscard]] virtual core::Result<void> row(std::uint32_t index, std::span<std::uint8_t> bytes,
                                                 RowUse use) = 0;
};
} // namespace docenhance::image
