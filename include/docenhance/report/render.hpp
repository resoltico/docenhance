// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/app/dispatch.hpp"

#include <string>
namespace docenhance::report {
// How an outcome is presented. The wire format is owned here and nowhere else:
// schemas/foundation-result.schema.json declares every field the JSON form emits.
enum class Format : unsigned char { text, json };
struct Output {
    std::string out;
    std::string err;
};
[[nodiscard]] Output render(const app::Outcome& outcome, Format format);
} // namespace docenhance::report
