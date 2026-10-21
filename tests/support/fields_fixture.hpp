// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/core/range.hpp"
#include "docenhance/methods/parameter_domains.hpp"
#include "docenhance/record/json.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace docenhance::tests {
enum class WireMode : std::uint8_t { plain = 0, textured = 1, unlisted = 255 };
struct WirePng {
    std::uint32_t width = 1;
    bool operator==(const WirePng&) const = default;
};
struct WireJpeg {
    std::uint32_t height = 1;
    bool operator==(const WireJpeg&) const = default;
};
struct WireText {
    std::string text;
    bool operator==(const WireText&) const = default;
};
struct WirePacket {
    std::string identity = "deadbeef";
    std::uint8_t count = 7;
    std::optional<double> measure;
    std::optional<std::uint32_t> cell;
    std::optional<double> target;
    WireMode mode = WireMode::plain;
    std::array<std::uint16_t, 2> samples{0, 65535};
    std::vector<bool> flags{true, false};
    std::variant<WirePng, WireJpeg> container;
    bool operator==(const WirePacket&) const = default;
};
struct WireLight {
    std::uint64_t eligible = 1;
    std::uint64_t changed = 0;
    bool operator==(const WireLight&) const = default;
};
inline bool validate(const WireLight& value) noexcept {
    return value.changed <= value.eligible;
}
struct WireNoise {
    double value = 0;
    bool operator==(const WireNoise&) const = default;
};
struct WireCompleted {
    std::optional<WireLight> illumination;
    std::optional<WireNoise> denoising;
    bool operator==(const WireCompleted&) const = default;
};
struct WireStages {
    std::optional<WireLight> illumination;
    std::optional<WireNoise> denoising;
    bool operator==(const WireStages&) const = default;
};
struct WireOddCollection {
    std::vector<bool> flags{true};
    bool operator==(const WireOddCollection&) const = default;
};
struct WireIntegralRange {
    std::optional<std::uint32_t> value;
    bool operator==(const WireIntegralRange&) const = default;
};
struct WireFloatingRange {
    double value = 2;
    bool operator==(const WireFloatingRange&) const = default;
};
struct WireReadTrap {
    std::uint32_t value = 1;
    bool operator==(const WireReadTrap&) const = default;
};
[[nodiscard]] inline bool read(const record::Json& /*document*/, WireReadTrap& /*value*/) {
    return true;
}
struct WireEmpty {
    bool operator==(const WireEmpty&) const = default;
};
} // namespace docenhance::tests

namespace docenhance {
template <> struct core::EnumNames<tests::WireMode> {
    static constexpr auto list = std::array{
        std::pair{tests::WireMode::plain, std::string_view{"plain"}},
        std::pair{tests::WireMode::textured, std::string_view{"textured"}},
    };
};
template <> struct core::WireTag<tests::WirePng> {
    static constexpr std::string_view key = "format";
    static constexpr std::string_view value = "png";
};
template <> struct core::WireTag<tests::WireJpeg> {
    static constexpr std::string_view key = "format";
    static constexpr std::string_view value = "jpeg";
};
template <> struct core::WireTag<tests::WireText> {
    static constexpr std::string_view key = "format";
    static constexpr std::string_view value = "text";
};
template <> struct core::WireTag<tests::WireLight> {
    static constexpr std::string_view key = "method";
    static constexpr std::string_view value = "I01";
};
template <> struct core::WireTag<tests::WireNoise> {
    static constexpr std::string_view key = "method";
    static constexpr std::string_view value = "D01";
};
template <> struct core::Fields<tests::WirePng> {
    static constexpr auto list = std::tuple{
        core::field("width", &tests::WirePng::width,
                    core::Range<std::uint32_t>{.minimum = 1, .maximum = 1024}),
    };
};
template <> struct core::Fields<tests::WireJpeg> {
    static constexpr auto list = std::tuple{
        core::field("height", &tests::WireJpeg::height,
                    core::Range<std::uint32_t>{.minimum = 1, .maximum = 1024}),
    };
};
template <> struct core::Fields<tests::WireText> {
    static constexpr auto list = std::tuple{core::field("text", &tests::WireText::text)};
};
template <> struct core::Fields<tests::WirePacket> {
    static constexpr auto list = std::tuple{
        core::field("identity", &tests::WirePacket::identity, core::Hex{.digits = 8}),
        core::field("count", &tests::WirePacket::count, core::Count{}),
        core::field("measure", &tests::WirePacket::measure, core::Unit{}),
        core::field(
            "cell", &tests::WirePacket::cell,
            core::ScalarChoice{.numeric = methods::background_cell_domain, .empty = "auto"}),
        core::field(
            "target", &tests::WirePacket::target,
            core::ScalarChoice{.numeric = methods::background_target_domain, .empty = "source"}),
        core::field("mode", &tests::WirePacket::mode),
        core::field("samples", &tests::WirePacket::samples),
        core::field("flags", &tests::WirePacket::flags,
                    core::Range<std::size_t>{.minimum = 0, .maximum = 2}),
        core::field("container", &tests::WirePacket::container),
    };
};
template <> struct core::Fields<tests::WireLight> {
    static constexpr auto list = std::tuple{
        core::field("eligible", &tests::WireLight::eligible, core::Count{}),
        core::field("changed", &tests::WireLight::changed, core::Count{}),
    };
};
template <> struct core::Fields<tests::WireNoise> {
    static constexpr auto list = std::tuple{
        core::field("value", &tests::WireNoise::value, core::Unit{}),
    };
};
template <> struct core::Fields<tests::WireCompleted> {
    static constexpr auto list = std::tuple{
        core::field("illumination", &tests::WireCompleted::illumination),
        core::field("denoising", &tests::WireCompleted::denoising),
    };
};
template <> struct core::Fields<tests::WireStages> {
    static constexpr auto list = std::tuple{
        core::field("illumination", &tests::WireStages::illumination),
        core::field("denoising", &tests::WireStages::denoising),
    };
};
template <> struct core::Fields<tests::WireOddCollection> {
    static constexpr auto list = std::tuple{
        core::field("flags", &tests::WireOddCollection::flags,
                    core::Range<std::size_t>{.minimum = 1, .maximum = 3, .odd = true}),
    };
};
template <> struct core::Fields<tests::WireIntegralRange> {
    static constexpr auto list = std::tuple{
        core::field(
            "value", &tests::WireIntegralRange::value,
            core::ScalarChoice{
                .numeric = core::Range<std::uint32_t>{.minimum = 1, .maximum = 7, .odd = true},
                .empty = "auto",
            }),
    };
};
template <> struct core::Fields<tests::WireFloatingRange> {
    static constexpr auto list = std::tuple{
        core::field("value", &tests::WireFloatingRange::value,
                    core::Range<double>{.minimum = 0, .maximum = 4, .odd = true}),
    };
};
template <> struct core::WireTag<tests::WireReadTrap> {
    static constexpr std::string_view key = "kind";
    static constexpr std::string_view value = "trap";
};
template <> struct core::Fields<tests::WireReadTrap> {
    static constexpr auto list = std::tuple{
        core::field("value", &tests::WireReadTrap::value,
                    core::Range<std::uint32_t>{.minimum = 1, .maximum = 4}),
    };
};
template <> struct core::Fields<tests::WireEmpty> {
    static constexpr auto list = std::tuple{};
};
} // namespace docenhance

template <>
inline constexpr bool docenhance::core::sparse_fields<docenhance::tests::WireCompleted> = true;
