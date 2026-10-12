// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/methods/stages.hpp"
namespace docenhance::app {
[[nodiscard]] bool valid_published(const PublishedBinary& value, const ProcessRequest& request);
[[nodiscard]] bool valid_published(const PublishedContinuous& value, const ProcessRequest& request);
[[nodiscard]] bool valid_partial_stages(const methods::StageObservations& stages,
                                        const ProcessRequest& request);
[[nodiscard]] bool valid_failure(const ProcessFailure& value, const ProcessRequest& request);
[[nodiscard]] bool valid_verified(const Verified& value, const VerifyRequest& request);
} // namespace docenhance::app
