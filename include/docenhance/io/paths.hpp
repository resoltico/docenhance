// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <string>

namespace docenhance::io {
// The name a record keeps for a file: the final component, without the directories that led to
// it. An absolute path is never recorded, and a name can still carry personal information, so a
// bundle is not described as anonymized.
[[nodiscard]] std::string file_name(const std::string& path);

} // namespace docenhance::io
