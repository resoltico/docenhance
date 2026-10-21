// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/publication.hpp"
#include "docenhance/record/inventory.hpp"

#include <string>
namespace docenhance::host {
struct VerifiedBundle {
    record::RunRecord declared;
    core::ContentIdentity record;
};
struct ExpectedBundle {
    std::string run;
    core::ContentIdentity record;
};
[[nodiscard]] fs::BundleValidation bundle_validation(ExpectedBundle& expected);
[[nodiscard]] core::Result<VerifiedBundle> verify_bundle(const std::string& directory,
                                                         const core::Cancellation& cancellation);
} // namespace docenhance::host
