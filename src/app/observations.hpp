// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/app/verify.hpp"
namespace docenhance::app {
[[nodiscard]] bool valid_published(const PublishedBinary& value, const ProcessRequest& request);
[[nodiscard]] bool valid_published(const PublishedContinuous& value, const ProcessRequest& request);
[[nodiscard]] bool valid_failure(const ProcessFailure& value, const ProcessRequest& request);
[[nodiscard]] bool valid_verified(const Verified& value, const VerifyRequest& request);
} // namespace docenhance::app
