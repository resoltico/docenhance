// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source_facts.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>
namespace docenhance::image {
enum class ToneMode { preserve, gray };
enum class OutputDepth { automatic, byte, word };
enum class AlphaPolicy { white, black, reject };
enum class ProfilePolicy { embedded, srgb };
enum class Interpretation { assumed_srgb, overridden_srgb, srgb, cicp, icc, gamma, chromaticities };
enum class InterpretationCategory { gray1, color3 };
class PngDepth {
  public:
    constexpr PngDepth() noexcept = default;
    [[nodiscard]] static constexpr std::optional<PngDepth> from_bits(unsigned bits) noexcept {
        if (bits != 1 && bits != byte_bits && bits != word_bits) {
            return std::nullopt;
        }
        return PngDepth{bits};
    }
    [[nodiscard]] constexpr unsigned bits() const noexcept {
        return bits_;
    }
    constexpr bool operator==(const PngDepth&) const = default;

  private:
    explicit constexpr PngDepth(unsigned bits) noexcept : bits_(bits) {}
    unsigned bits_ = byte_bits;
};
struct PngShape {
    std::uint32_t width{};
    std::uint32_t height{};
    SampleModel model = SampleModel::gray;
    PngDepth depth;
    bool operator==(const PngShape&) const = default;
};
struct InterpretationPolicy {
    AlphaPolicy alpha = AlphaPolicy::white;
    ProfilePolicy profile = ProfilePolicy::embedded;
    QuarterTurn rotation;
};
struct BlockObservation {
    std::uint64_t flattened_pixels{};
    std::uint64_t clipped_components{};
    bool operator==(const BlockObservation&) const = default;
};
struct InterpretationReport {
    RasterShape decoded;
    PngShape output;
    Interpretation interpretation = Interpretation::assumed_srgb;
    Orientation orientation;
    QuarterTurn rotation;
    std::optional<Resolution> resolution;
    std::uint64_t flattened_pixels{};
    std::uint64_t clipped_components{};
    bool assumed_transfer = false;
    bool assumed_primaries = false;
    bool depth_reduced = false;
    bool operator==(const InterpretationReport&) const = default;
};
[[nodiscard]] InterpretationCategory interpretation_category(SampleModel model) noexcept;
[[nodiscard]] bool valid_interpretation_policy(InterpretationPolicy policy) noexcept;
[[nodiscard]] bool validate(PngShape shape) noexcept;
[[nodiscard]] bool validate(const InterpretationReport& report) noexcept;
[[nodiscard]] std::string_view interpretation_name(Interpretation value) noexcept;
} // namespace docenhance::image

namespace docenhance {
template <> struct core::EnumNames<image::ToneMode> {
    using Value = image::ToneMode;
    static constexpr std::array list{
        std::pair{Value::preserve, std::string_view{"preserve"}},
        std::pair{Value::gray, std::string_view{"gray"}},
    };
};
template <> struct core::EnumNames<image::OutputDepth> {
    using Value = image::OutputDepth;
    static constexpr std::array list{
        std::pair{Value::automatic, std::string_view{"auto"}},
        std::pair{Value::byte, std::string_view{"8"}},
        std::pair{Value::word, std::string_view{"16"}},
    };
};
template <> struct core::EnumNames<image::AlphaPolicy> {
    using Value = image::AlphaPolicy;
    static constexpr std::array list{
        std::pair{Value::white, std::string_view{"white"}},
        std::pair{Value::black, std::string_view{"black"}},
        std::pair{Value::reject, std::string_view{"reject"}},
    };
};
template <> struct core::EnumNames<image::ProfilePolicy> {
    using Value = image::ProfilePolicy;
    static constexpr std::array list{
        std::pair{Value::embedded, std::string_view{"embedded"}},
        std::pair{Value::srgb, std::string_view{"srgb"}},
    };
};
template <> struct core::EnumNames<image::Interpretation> {
    using Value = image::Interpretation;
    static constexpr std::array list{
        std::pair{Value::assumed_srgb, std::string_view{"assumed_srgb"}},
        std::pair{Value::overridden_srgb, std::string_view{"overridden_srgb"}},
        std::pair{Value::srgb, std::string_view{"srgb"}},
        std::pair{Value::cicp, std::string_view{"cicp_srgb"}},
        std::pair{Value::icc, std::string_view{"icc"}},
        std::pair{Value::gamma, std::string_view{"png_gamma"}},
        std::pair{Value::chromaticities, std::string_view{"png_chromaticities"}},
    };
};
template <> struct core::Fields<image::PngShape> {
    using Value = image::PngShape;
    static constexpr auto list = std::tuple{
        core::field("width", &Value::width, core::Count{}),
        core::field("height", &Value::height, core::Count{}),
        core::field("channels", &Value::model,
                    core::Choices{std::array{image::SampleModel::gray, image::SampleModel::rgb}}),
        core::field("bit_depth", &Value::depth),
    };
};
template <> struct core::Fields<image::InterpretationReport> {
    using Value = image::InterpretationReport;
    static constexpr auto list = std::tuple{
        core::field("decoded", &Value::decoded),
        core::field("output", &Value::output),
        core::field("profile_decision", &Value::interpretation),
        core::field("source_orientation", &Value::orientation),
        core::field("rotation", &Value::rotation),
        core::field("resolution", &Value::resolution),
        core::field("alpha_composited_pixels", &Value::flattened_pixels, core::Count{}),
        core::field(
            "clipped_components", &Value::clipped_components,
            core::Range<std::uint64_t>{.minimum = 0, .maximum = 3 * image::source_pixels_max}),
        core::field("assumed_transfer", &Value::assumed_transfer),
        core::field("assumed_primaries", &Value::assumed_primaries),
        core::field("depth_reduced", &Value::depth_reduced),
    };
};
} // namespace docenhance
