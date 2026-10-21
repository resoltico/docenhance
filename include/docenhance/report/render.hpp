// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/dispatch.hpp"

#include <string>
namespace docenhance::report {
// How the retained outcome is presented. Actual typed response Fields own the wire shape;
// native schema emission derives its exported artifact from those same values.
enum class Format : unsigned char { text, json };
struct Output {
    std::string out;
    std::string err;
};
[[nodiscard]] core::Result<Output> render(const app::Outcome& outcome, Format format);
} // namespace docenhance::report
