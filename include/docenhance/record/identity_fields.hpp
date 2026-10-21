// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"

#include <cstdint>
#include <tuple>
namespace docenhance {
template <> struct core::Fields<core::ContentIdentity> {
    static constexpr auto list = std::tuple{
        core::field("sha256", &core::ContentIdentity::sha256,
                    core::Hex{.digits = core::sha256_hex_length}),
        core::field(
            "bytes", &core::ContentIdentity::bytes,
            core::Range<std::uint64_t>{.minimum = 1, .maximum = core::bundle_max_file_bytes}),
    };
};
} // namespace docenhance
