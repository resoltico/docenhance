// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/identity.hpp"

#include "docenhance/version.hpp"

namespace docenhance::core {
BuildFacts build_facts() noexcept {
    return {
        .version = application_version,
        .platform = build_platform,
        .compiler = build_compiler,
        .dependency_lock_sha256 = dependency_lock_sha256,
    };
}
} // namespace docenhance::core
