// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <string>

namespace docenhance::host {
// An instant in UTC as RFC 3339 spells it, "YYYY-MM-DDTHH:MM:SSZ", from seconds since the epoch.
//
// The calendar conversion is computed here rather than taken from the standard library's, because
// both implementations of it add a negative offset to an unsigned month. That is well-defined
// arithmetic, but it is an implicit sign change, which is one of the conversions this project's
// sanitizer builds treat as a finding. This produces the same civil date with no intermediate that
// changes sign, and no dependency on a locale or on which standard library is in use.
//
// Defined for years 0000 through 9999, which contains every instant a system clock holds in
// practice. Outside it the year is written with as many digits as it needs and is no longer the
// fixed-width form RFC 3339 defines.
[[nodiscard]] std::string rfc3339_utc(std::int64_t seconds_since_epoch);
} // namespace docenhance::host
