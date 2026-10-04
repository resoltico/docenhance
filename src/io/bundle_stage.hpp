// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "entry_identity.hpp"
#include "publication.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>
namespace docenhance::io {
struct OwnedEntry {
    std::filesystem::path path;
    EntryLease owner;
    bool directory = false;
};
struct Stage {
    std::filesystem::path directory;
    // Everything this invocation created, newest first, so cleanup removes only its own work.
    std::vector<OwnedEntry> created;
    EntryLease owner;
    bool owned = false;
    bool retained = false;
    bool committed = false;
    core::Error uncertain{
        .code = core::ErrorCode::publication_unknown,
        .message = "Publication cannot be reconciled; inspect output and retained staging",
        .publication = core::Publication::unknown,
    };
    core::Error integrity{
        .code = core::ErrorCode::output_verify,
        .message = "Publication completed but bundle integrity could not be confirmed",
        .publication = core::Publication::completed,
    };
    Stage() = default;
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;
    Stage(Stage&&) = delete;
    Stage& operator=(Stage&&) = delete;
    [[nodiscard]] bool owns_entries() const {
        if (!owned || !owner.matches(directory)) {
            return false;
        }
        return std::ranges::all_of(
            created, [](const OwnedEntry& entry) { return entry.owner.matches(entry.path); });
    }
    // Never recursively delete: a foreign entry is evidence that cleanup cannot be guaranteed.
    [[nodiscard]] bool cleanup() noexcept {
        if (!owned) {
            return true;
        }
        try {
            if (!owner.matches(directory)) {
                return false;
            }
            // Validate every owned directory before touching any descendant. A replaced assets
            // directory must never become a route through a link during cleanup.
            for (const auto& entry : created) {
                if (entry.directory && !entry.owner.matches(entry.path)) {
                    return false;
                }
            }
            bool failed = false;
            for (auto& entry : created) {
                if (!entry.owner.matches(entry.path)) {
                    std::error_code status_error;
                    const auto status = std::filesystem::symlink_status(entry.path, status_error);
                    const bool absent = status.type() == std::filesystem::file_type::not_found;
                    failed = failed || !absent;
                    continue;
                }
                std::error_code error;
                std::filesystem::remove(entry.path, error);
                failed = failed || static_cast<bool>(error);
                if (!error) {
                    entry.owner = {}; // Finish Windows delete-pending removal before its parent.
                }
            }
            std::error_code directory_error;
            std::filesystem::remove(directory, directory_error);
            if (!directory_error) {
                owner = {};
            }
            owned = failed || static_cast<bool>(directory_error);
            return !owned;
        } catch (...) {
            // Even allocation failure in a filesystem error-code overload cannot escape cleanup.
            return false;
        }
    }
    ~Stage() {
        if (!retained) {
            static_cast<void>(cleanup());
        }
    }
};
[[nodiscard]] core::Error abandon(Stage& stage, core::Error error);
[[nodiscard]] core::Result<void> reserve_stage(Stage& stage, const std::filesystem::path& target,
                                               const core::Cancellation& cancellation);
[[nodiscard]] core::Result<BundleSlot> place(Stage& stage, std::string_view relative);
} // namespace docenhance::io
