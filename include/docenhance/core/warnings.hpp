// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace docenhance::core {
enum class Warning : std::uint8_t {
    profile_assumed,
    profile_overridden,
    alpha_flattened,
    depth_reduced,
    tv_iteration_limit,
    restoration_inference,
    psf_after_transform,
    psf_off_center,
    sharpening,
};
// At most four warning identities come from one report projection; they are never run-record state.
inline constexpr std::size_t warning_projection_capacity = 4;
using WarningList = std::array<std::optional<Warning>, warning_projection_capacity>;
template <> struct EnumNames<Warning> {
    static constexpr std::array list{
        std::pair{Warning::profile_assumed, std::string_view{"W_PROFILE_ASSUMED"}},
        std::pair{Warning::profile_overridden, std::string_view{"W_PROFILE_OVERRIDDEN"}},
        std::pair{Warning::alpha_flattened, std::string_view{"W_ALPHA_FLATTENED"}},
        std::pair{Warning::depth_reduced, std::string_view{"W_DEPTH_REDUCED"}},
        std::pair{Warning::tv_iteration_limit, std::string_view{"W_TV_ITERATION_LIMIT"}},
        std::pair{Warning::restoration_inference, std::string_view{"W_RESTORATION_INFERENCE"}},
        std::pair{Warning::psf_after_transform, std::string_view{"W_PSF_AFTER_TRANSFORM"}},
        std::pair{Warning::psf_off_center, std::string_view{"W_PSF_OFF_CENTER"}},
        std::pair{Warning::sharpening, std::string_view{"W_SHARPENING"}},
    };
};
} // namespace docenhance::core
