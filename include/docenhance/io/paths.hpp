// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <string>

namespace docenhance::io {
// This helper returns only the final component, without its containing directories. Source
// names use it; explicit request paths such as a supplied PSF retain their admitted spelling.
// Names and paths can carry personal information, so bundles are not described as anonymized.
[[nodiscard]] std::string file_name(const std::string& path);

} // namespace docenhance::io
