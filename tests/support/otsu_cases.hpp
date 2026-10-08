// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/otsu.hpp"
#include "otsu_reference.hpp"
#include "require.hpp"

#include <cstdint>
namespace docenhance::tests {
inline void otsu_cases() {
    constexpr std::uint32_t byte_values = 256;
    core::Budget budget{65536};
    auto source = image::Plane<std::uint8_t>::allocate(budget, byte_values, 1).value();
    auto output = image::Plane<std::uint8_t>::allocate(budget, byte_values, 1).value();
    for (std::uint32_t x = 0; x < byte_values; ++x) {
        source.view().row(0).subspan(x, 1).front() = static_cast<std::uint8_t>(x);
    }
    const auto held = budget.used();
    const auto observation = methods::fit_otsu(source.view().as_const(), budget);
    require(observation.has_value(), "all byte values fit global Otsu");
    require(budget.used() == held, "histogram fit refunds its charge");
    require(*observation == otsu_reference(source.view().as_const()),
            "all byte values match independent population oracle");
    require(methods::apply_otsu(source.view().as_const(), output.view(), *observation).has_value(),
            "frozen Otsu split applies");
    for (std::uint32_t x = 0; x < byte_values; ++x) {
        const auto bin = otsu_reference_bin(static_cast<std::uint8_t>(x));
        require(output.view().row(0).subspan(x, 1).front() ==
                    (bin <= observation->threshold_bin ? 0 : UINT8_MAX),
                "all byte values retain exact black and white endpoints");
    }
}
} // namespace docenhance::tests
