// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/otsu.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/image/plane.hpp"
#include "otsu_reference.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace {
constexpr std::size_t budget_limit = 65536;
constexpr std::uint32_t maximum_extent = 13;
std::uint8_t byte_at(std::span<const std::uint8_t> bytes, std::size_t index) {
    return index < bytes.size() ? bytes.subspan(index, 1).front() : 0;
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using docenhance::fuzz::require;
    namespace core = docenhance::core;
    namespace image = docenhance::image;
    namespace methods = docenhance::methods;
    namespace tests = docenhance::tests;
    const std::span<const std::uint8_t> bytes{data, size};
    const auto width = 1U + (byte_at(bytes, 0) % maximum_extent);
    const auto height = 1U + (byte_at(bytes, 1) % maximum_extent);
    core::Budget budget{budget_limit};
    auto source = image::Plane<std::uint8_t>::allocate(budget, width, height).value();
    auto output = image::Plane<std::uint8_t>::allocate(budget, width, height).value();
    constexpr std::size_t header_bytes = 2;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            source.view().row(y).subspan(x, 1).front() =
                byte_at(bytes, header_bytes + (static_cast<std::size_t>(y) * width) + x);
        }
    }
    const auto held = budget.used();
    const auto fitted = methods::fit_otsu(source.view().as_const(), budget);
    require(fitted.has_value(), "valid Otsu source fits");
    require(budget.used() == held, "histogram charge is refunded");
    require(*fitted == tests::otsu_reference(source.view().as_const()),
            "fitted Otsu split matches independently enumerated populations");
    require(methods::apply_otsu(source.view().as_const(), output.view(), *fitted).has_value(),
            "frozen Otsu fit applies");
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto bin = tests::otsu_reference_bin(source.view().row(y).subspan(x, 1).front());
            require(output.view().row(y).subspan(x, 1).front() ==
                        (bin <= fitted->threshold_bin ? 0 : UINT8_MAX),
                    "exact Otsu binary polarity");
        }
    }
    return 0;
}
