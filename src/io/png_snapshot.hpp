// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"

#include <string>
namespace docenhance::io {
[[nodiscard]] core::Result<core::Buffer> read_png_snapshot(const std::string& input,
                                                           core::Budget& budget,
                                                           const core::Cancellation& cancellation);
} // namespace docenhance::io
