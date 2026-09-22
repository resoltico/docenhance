// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/contract/cli_contract.hpp"
#include "docenhance/contract/parse.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/io/capabilities.hpp"
#include "docenhance/methods/catalog.hpp"
#include "require.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace docenhance::tests {
inline void parser_cases() {
    const auto all = contract::parse_pages("all");
    require(all.has_value() && all.value().all && all.value().pages.empty(), "all pages");
    const auto pages = contract::parse_pages("5,1-3");
    require(pages.has_value() && pages.value().pages == std::vector<std::uint32_t>({1, 2, 3, 5}),
            "sorted page expansion");
    for (const auto* const text : {
             "",
             "0",
             "1,",
             ",1",
             "1,,2",
             "2-1",
             "1-",
             "-1",
             "1-2-3",
             "1,1",
             "1-3,2",
             "1 2",
             "+1",
             "ALL",
             "4294967296",
             "1.0",
         }) {
        require(!contract::parse_pages(text).has_value(),
                std::string("bad page input accepted: ") + text);
    }
    require(!contract::parse_pages("1-100", 99).has_value(), "bounded page expansion");
    require(contract::parse_pages("4294967295").has_value(), "uint32 maximum page");
    require(!contract::parse_pages("1-4294967295").has_value(),
            "hostile expansion fails before allocation");
    require(contract::parse_finite("1e-2", 0, 1).value() == 0.01, "finite exponent");
    for (const auto* const text : {
             "nan", "NaN", "inf", "-inf", "1e999", "0x1.0p0", " 1",  "1 ",
             "1,2", "",    "+1",  "\t1",  "\n1",   "\v1",     "\f1", "\r1",
             "1\t", "-",   ".",   "e1",   "1e",    "1e-",     "--1", "1..0",
         }) {
        require(!contract::parse_finite(text, -10, 10).has_value(),
                "noncanonical/invalid decimal accepted");
    }
    require(!contract::parse_finite("2", 0, 1).has_value(), "range enforced");
    require(!contract::parse_finite("0", 1, 0).has_value(), "invalid bounds rejected");
    require(image::checked_elements(12, 10, 3, 360).value() == 360, "exact raster budget");
    require(!image::checked_elements(12, 10, 3, 359).has_value(), "budget overrun");
    require(!image::checked_elements(std::numeric_limits<std::size_t>::max(), 2, 4,
                                     std::numeric_limits<std::size_t>::max())
                 .has_value(),
            "overflow prevented");
    require(!image::checked_elements(0, 2, 3, 100).has_value(), "zero raster rejected");
}
// The decimal grammar of spec/cli-contract.json, checked by spelling rather than against the
// parser's own scanner: equivalent forms, malformed exponents and option ranges.
inline void decimal_cases() {
    const auto hundred = contract::parse_finite("1e2", -1000, 1000);
    require(hundred.has_value() && hundred.value() == 100.0, "1e2 is one hundred");
    for (const auto* const text : {
             "1e+2",
             "1E2",
             "1E+2",
             "100",
             "100.",
             "100.0",
             "1.0e2",
             "0.1e+3",
             "10000e-2",
         }) {
        const auto value = contract::parse_finite(text, -1000, 1000);
        require(value.has_value() && value.value() == hundred.value(),
                std::string("equivalent spelling of 1e2: ") + text);
    }
    require(contract::parse_finite("-1e+2", -1000, 1000).value() == -100.0,
            "signed exponent, negative");
    require(contract::parse_finite("1e-2", 0, 1).value() == 0.01, "negative exponent");
    require(contract::parse_finite(".5e+1", 0, 10).value() == 5.0,
            "leading point with signed exponent");
    // A '+' is an exponent sign only; malformed exponents and trailing characters are rejected.
    for (const auto* const text : {
             "+1",    "+1e2",  "+1e+2", "1e",    "1e+",   "1e-",   "1e++2",  "1e--2",
             "1e+-2", "1e-+2", "1+e2",  "1e +2", "1e+ 2", "1e+2+", "1e+2x",  "1e+2.5",
             "1.5e",  "e+2",   "E+2",   ".e+2",  "1e+2 ", " 1e+2", "1e+2\t", "1,e+2",
         }) {
        require(!contract::parse_finite(text, -1000, 1000).has_value(),
                std::string("malformed decimal accepted: ") + text);
    }
    // Grammatical scientific notation still has to lie inside the option's range and be finite.
    require(!contract::parse_finite("1e+2", 0, 1).has_value(), "in-grammar value above the range");
    require(!contract::parse_finite("-1e+2", -10, 10).has_value(),
            "in-grammar value below the range");
    require(contract::parse_finite("1e-8", 1e-8, 1e-3).has_value(),
            "range endpoint in exponent form");
    require(!contract::parse_finite("1e-9", 1e-8, 1e-3).has_value(), "below the option range");
    require(!contract::parse_finite("1e+400", -1e308, 1e308).has_value(), "overflowing exponent");
    // Integer-valued inputs never gain scientific notation.
    for (const auto* const text : {"1e2", "1e+2", "1E2", "2e0-3"}) {
        require(!contract::parse_pages(text).has_value(),
                std::string("page selection accepted an exponent: ") + text);
    }
}
inline void numerical_cases() {
    require(image::srgb_decode(0).value() == 0, "black decode");
    require(image::srgb_decode(1).value() == 1, "white decode");
    require(std::abs(image::srgb_decode(0.04045).value() - (0.04045 / 12.92)) < 1e-14,
            "sRGB low branch boundary");
    for (unsigned i = 0; i <= 4096; ++i) {
        const double encoded = static_cast<double>(i) / 4096.0;
        const auto linear = image::srgb_decode(encoded);
        const auto roundtrip = image::srgb_encode(linear.value());
        require(std::abs(encoded - roundtrip.value()) < 3e-8, "sRGB roundtrip");
    }
    for (const double value : {
             -0.01,
             1.01,
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN(),
         }) {
        require(!image::srgb_decode(value).has_value(), "bad sRGB decode rejected");
        require(!image::srgb_encode(value).has_value(), "bad sRGB encode rejected");
    }
    const auto colors = std::to_array<image::Rgb>({
        {0, 0, 0},
        {1, 1, 1},
        {1, 0, 0},
        {0, 1, 0},
        {0, 0, 1},
        {0.1, 0.7, 0.9},
        {0.98, 0.99, 1},
    });
    for (const auto& rgb : colors) {
        for (unsigned k = 0; k <= 100; ++k) {
            const double target = static_cast<double>(k) / 100.0;
            const auto result = image::transport_luminance(rgb, target);
            require(result.has_value(), "luminance transport exists");
            require(std::abs(image::luminance(result.value()).value() - target) < 2e-15,
                    "target luminance");
            for (const double c : result.value()) {
                require(c >= 0 && c <= 1, "gamut bounded");
            }
        }
    }
    const std::array<double, 5> values{9, 1, 3, 3, 7};
    require(image::nearest_rank(values, 0).value() == 1, "percentile p=0");
    require(image::nearest_rank(values, 1).value() == 9, "percentile p=1");
    require(image::nearest_rank(values, 0.5).value() == 3, "nearest-rank median");
    require(!image::nearest_rank({}, 0.5).has_value(), "empty percentile rejected");
    for (std::int64_t x = -64; x <= 64; ++x) {
        require(image::reflect101(x, 1).value() == 0, "singleton reflection");
        std::int64_t slow = x;
        while (slow < 0 || slow > 4) {
            slow = slow < 0 ? -slow : 8 - slow;
        }
        require(std::cmp_equal(image::reflect101(x, 5).value(), slow),
                "global reflect101 reference");
    }
    require(image::reflect101(std::numeric_limits<std::int64_t>::min(), 5).has_value(),
            "reflection handles INT64_MIN");
    require(!image::reflect101(0, 0).has_value(), "empty reflection rejected");
}
inline void capabilities_cases() {
    require(contract::option_catalog.size() == 6, "compiled target contract size");
    // The reviewed scope of an option is typed, so no layer has to interpret a scope string.
    const auto& out_dir = contract::option_catalog.front();
    require(out_dir.name == "--out-dir", "the catalog keeps the reviewed order");
    require(out_dir.scope.contains(contract::Command::process), "--out-dir belongs to process");
    require(!out_dir.scope.contains(contract::Command::methods),
            "--out-dir is not a methods option");
    require(!out_dir.scope.contains(contract::Command::root), "--out-dir is not a root option");
    require(contract::CommandSet::all().contains(contract::Command::version),
            "every command is in all");
    require(!contract::CommandSet{}.contains(contract::Command::root),
            "an empty scope holds nothing");
    require(!contract::command_usage(contract::Command::process).empty(),
            "every command has a usage line");
    require(methods::implemented_methods().size() == 1 &&
                methods::implemented_methods().front().id == "B03",
            "B03 is the only advertised method");
    require(io::supported_input_formats().size() == 1 &&
                io::supported_input_formats().front() == "png",
            "PNG is the only advertised input format");
    const core::Error unavailable{.code = core::ErrorCode::unavailable, .message = "not ready"};
    require(unavailable.exit_code() == core::ExitCode::processing,
            "unavailable is not publication-unknown");
    require(unavailable.identifier() == "E_NOT_IMPLEMENTED", "stable unavailable diagnostic");
}
} // namespace docenhance::tests
