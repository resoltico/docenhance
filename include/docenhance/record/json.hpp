// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string_view>

namespace docenhance::record {
using Json = nlohmann::ordered_json;

struct JsonLimits {
    std::size_t bytes;
    std::size_t depth;
    std::size_t events;
};

// Admission precedes DOM construction so discarded members cannot hide duplicate keys or work.
[[nodiscard]] core::Result<void> admit_json(std::string_view text, JsonLimits limits);
} // namespace docenhance::record
