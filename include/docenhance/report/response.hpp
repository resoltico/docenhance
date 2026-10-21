// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/response.hpp"
#include "docenhance/record/json.hpp"

namespace docenhance::report {
[[nodiscard]] core::Result<app::WireResponse> response_value(const app::Outcome& outcome);
[[nodiscard]] app::ResponseCapabilities response_capabilities(const app::Capabilities& value);
[[nodiscard]] core::Result<record::Json> encode_response(const app::WireResponse& response);
[[nodiscard]] core::Result<record::Json> response_schema();
} // namespace docenhance::report
