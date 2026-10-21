// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/options.hpp"
#include "docenhance/app/values.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/catalog.hpp"
#include "require.hpp"
#include "scalar_boundary_cases.hpp"

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
#include <variant>
#include <vector>
namespace docenhance::tests {
static_assert(noexcept(image::srgb_decode(0.0)));
static_assert(noexcept(image::srgb_encode(0.0)));
static_assert(noexcept(image::luminance(image::Rgb{})));
static_assert(noexcept(image::transport_luminance(image::Rgb{}, 0.0)));
static_assert(noexcept(image::blend_perceptual(image::Rgb{}, 0.0, 0.0, 0.0)));
inline void parser_cases() {
    require(app::parse_finite("1e-2", 0, 1).value() == 0.01, "finite exponent");
    for (const auto* const text : {
             "nan", "NaN", "inf", "-inf", "1e999", "0x1.0p0", " 1",  "1 ",
             "1,2", "",    "+1",  "\t1",  "\n1",   "\v1",     "\f1", "\r1",
             "1\t", "-",   ".",   "e1",   "1e",    "1e-",     "--1", "1..0",
         }) {
        require(!app::parse_finite(text, -10, 10).has_value(),
                "noncanonical/invalid decimal accepted");
    }
    const auto option = app::parse_decimal_option("--gamma", "5", 0.25, 4);
    require(!option && option.error().code == core::ErrorCode::argument &&
                option.error().message.contains("--gamma value \"5\"") &&
                option.error().message.contains("[0.25,4]"),
            "named decimal diagnostic");
    require(app::parse_integer_option("--sauvola-window", "31", 3, 4095, true).value() == 31,
            "named integer acceptance");
    for (const auto* const text : {"", "4", "+31", "31.0", "1e1", "4294967296"}) {
        const auto integer = app::parse_integer_option("--sauvola-window", text, 3, 4095, true);
        require(!integer && integer.error().message.contains("--sauvola-window") &&
                    integer.error().message.contains("[3,4095]"),
                "named integer refusal");
    }
    require(!app::parse_finite("2", 0, 1).has_value(), "range enforced");
    require(!app::parse_finite("0", 1, 0).has_value(), "invalid bounds rejected");
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
    const auto hundred = app::parse_finite("1e2", -1000, 1000);
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
        const auto value = app::parse_finite(text, -1000, 1000);
        require(value.has_value() && value.value() == hundred.value(),
                std::string("equivalent spelling of 1e2: ") + text);
    }
    require(app::parse_finite("-1e+2", -1000, 1000).value() == -100.0, "signed exponent, negative");
    require(app::parse_finite("1e-2", 0, 1).value() == 0.01, "negative exponent");
    require(app::parse_finite(".5e+1", 0, 10).value() == 5.0, "leading point with signed exponent");
    // A '+' is an exponent sign only; malformed exponents and trailing characters are rejected.
    for (const auto* const text : {
             "+1",    "+1e2",  "+1e+2", "1e",    "1e+",   "1e-",   "1e++2",  "1e--2",
             "1e+-2", "1e-+2", "1+e2",  "1e +2", "1e+ 2", "1e+2+", "1e+2x",  "1e+2.5",
             "1.5e",  "e+2",   "E+2",   ".e+2",  "1e+2 ", " 1e+2", "1e+2\t", "1,e+2",
         }) {
        require(!app::parse_finite(text, -1000, 1000).has_value(),
                std::string("malformed decimal accepted: ") + text);
    }
    // Grammatical scientific notation still has to lie inside the option's range and be finite.
    require(!app::parse_finite("1e+2", 0, 1).has_value(), "in-grammar value above the range");
    require(!app::parse_finite("-1e+2", -10, 10).has_value(), "in-grammar value below the range");
    require(app::parse_finite("1e-8", 1e-8, 1e-3).has_value(), "range endpoint in exponent form");
    require(!app::parse_finite("1e-9", 1e-8, 1e-3).has_value(), "below the option range");
    require(!app::parse_finite("1e+400", -1e308, 1e308).has_value(), "overflowing exponent");
    require(!app::parse_finite("1e-400", 0, 1).has_value(), "conversion underflow refused");
    require(app::parse_finite("0e-400", 0, 1).value() == 0, "zero has no underflow");
    require(!app::parse_finite("4.9406564584124654e-324", 0, 1).has_value(),
            "conversion underflow is refused even for subnormal results");
    require(std::signbit(app::parse_finite("-0", -1, 1).value()), "negative zero retained");
}
inline void numerical_cases() {
    require(image::srgb_decode(0) == 0, "black decode");
    require(image::srgb_decode(1) == 1, "white decode");
    require(image::srgb_encode(1) == std::nextafter(1.0, 0.0),
            "white encoding retains the actual prescribed binary64 result below one");
    require(std::abs(image::srgb_decode(0.04045) - (0.04045 / 12.92)) < 1e-14,
            "sRGB low branch boundary");
    for (unsigned i = 0; i <= 4096; ++i) {
        const double encoded = static_cast<double>(i) / 4096.0;
        const auto linear = image::srgb_decode(encoded);
        require(std::isfinite(linear) && linear >= 0 && linear <= 1,
                "decoded transfer output is finite unit data before direct encoding");
        const auto roundtrip = image::srgb_encode(linear);
        require(std::abs(encoded - roundtrip) < 3e-8, "sRGB roundtrip");
    }
    quantization_domain_cases();
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
            for (const double c : result) {
                require(c >= 0 && c <= 1, "gamut bounded");
            }
            require(std::abs(image::luminance(result) - target) < 2e-15, "target luminance");
        }
    }
    std::array<double, 5> values{9, 1, 3, 3, 7};
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
inline void execution_option_cases() {
    for (const auto* const name : {"--threads", "--memory-limit"}) {
        require(std::ranges::count(app::option_catalog, name, &app::OptionDescriptor::name) == 1,
                "each execution option is declared exactly once");
    }
    require(std::ranges::any_of(app::option_catalog,
                                [](const app::OptionDescriptor& option) {
                                    return option.name == "--threads" &&
                                           option.id == app::OptionId::threads &&
                                           option.kind == app::ValueKind::auto_or_integer &&
                                           option.scope.contains(app::Command::process) &&
                                           !option.scope.contains(app::Command::root) &&
                                           !option.scope.contains(app::Command::verify) &&
                                           !option.scope.contains(app::Command::methods) &&
                                           !option.scope.contains(app::Command::version) &&
                                           option.minimum == 1 && option.maximum == 256 &&
                                           option.default_value == std::string_view{"auto"} &&
                                           option.domain == "auto; auto | integer [1,256]";
                                }),
            "threads owns the process-only auto or 1..256 worker domain");
    require(std::ranges::any_of(app::option_catalog,
                                [](const app::OptionDescriptor& option) {
                                    return option.name == "--memory-limit" &&
                                           option.id == app::OptionId::memory_limit &&
                                           option.kind == app::ValueKind::integer &&
                                           option.scope.contains(app::Command::process) &&
                                           !option.scope.contains(app::Command::root) &&
                                           !option.scope.contains(app::Command::verify) &&
                                           !option.scope.contains(app::Command::methods) &&
                                           !option.scope.contains(app::Command::version) &&
                                           option.minimum == 64 && option.maximum == 262144 &&
                                           option.default_value == std::string_view{"1024"} &&
                                           option.domain == "1024; integer [64,262144]";
                                }),
            "memory limit owns the process-only 64..262144 MiB domain with 1024 default");
}
inline void capabilities_cases() {
    require(app::option_catalog.size() == 52, "compiled target contract size");
    execution_option_cases();
    // The reviewed scope of an option is typed, so no layer has to interpret a scope string.
    const auto& out_dir = app::option_catalog.front();
    require(out_dir.name == "--out-dir", "the catalog keeps the reviewed order");
    require(out_dir.scope.contains(app::Command::process), "--out-dir belongs to process");
    require(!out_dir.scope.contains(app::Command::methods), "--out-dir is not a methods option");
    require(!out_dir.scope.contains(app::Command::root), "--out-dir is not a root option");
    require(std::ranges::count(app::option_catalog, "--rotate", &app::OptionDescriptor::name) == 1,
            "rotation option is declared once");
    require(std::ranges::any_of(app::option_catalog,
                                [](const app::OptionDescriptor& option) {
                                    return option.name == "--rotate" &&
                                           option.scope.contains(app::Command::process) &&
                                           !option.scope.contains(app::Command::methods) &&
                                           !option.scope.contains(app::Command::root) &&
                                           option.id == app::OptionId::rotate &&
                                           option.kind == app::ValueKind::choice;
                                }),
            "rotation is process-only and owns its generated choice identity");
    require(app::CommandSet::all().contains(app::Command::version), "every command is in all");
    require(!app::CommandSet{}.contains(app::Command::root), "an empty scope holds nothing");
    require(!app::command_usage(app::Command::process).empty(), "every command has a usage line");
    require(
        methods::implemented_methods().size() == 12 &&
            methods::implemented_methods().front().id == "I01" &&
            methods::implemented_methods().back().id == "B03",
        "The compiled catalog lists processing methods; common geometry stays outside discovery");
    const core::Error not_implemented{
        .code = core::ErrorCode::not_implemented,
        .message = "not ready",
    };
    require(not_implemented.exit_code() == core::ExitCode::processing,
            "unimplemented capability uses processing refusal");
    require(not_implemented.identifier() == "E_NOT_IMPLEMENTED",
            "stable unimplemented capability diagnostic");
}
} // namespace docenhance::tests
