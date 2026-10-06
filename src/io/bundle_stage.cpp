// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "bundle_stage.hpp"

#include "docenhance/core/bundle_path.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/artifact_limits.hpp"
#include "docenhance/io/publication.hpp"
#include "entry_identity.hpp"
#include "file_access.hpp"

#include <algorithm>
#include <cstddef>
#ifndef _WIN32
#include <cerrno>
#include <expected>
#include <sys/stat.h>
#endif
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
namespace docenhance::io {
namespace {
constexpr unsigned staging_attempts = 64;
bool private_directory(const std::filesystem::path& path, std::error_code& error) {
#ifdef _WIN32
    return std::filesystem::create_directory(path, error);
#else
    constexpr unsigned int owner_only = 0700;
    if (mkdir(path.c_str(), owner_only) == 0) {
        error.clear();
        return true;
    }
    error = {errno, std::generic_category()};
    return false;
#endif
}
} // namespace
[[nodiscard]] core::Error abandon(Stage& stage, core::Error error) {
    error.publication =
        stage.owned ? core::Publication::not_published : core::Publication::not_started;
    if (!stage.cleanup()) {
        stage.retained = true;
        error.code = core::ErrorCode::publication_unknown;
        error.publication = core::Publication::unknown;
        error.message = "Publication did not finish; staging cleanup could not be confirmed";
    }
    return error;
}
[[nodiscard]] core::Result<void> reserve_stage(Stage& stage, const std::filesystem::path& target,
                                               const core::Cancellation& cancellation) {
    if (cancellation.requested(core::Checkpoint::staging)) {
        return core::cancelled();
    }
    const auto parent =
        target.parent_path().empty() ? std::filesystem::path{"."} : target.parent_path();
    std::error_code error;
    stage.created.reserve(bundle_max_entries);
    if (target.filename().empty() || target.filename() == "." || target.filename() == ".." ||
        !std::filesystem::is_directory(parent, error) || error) {
        return core::failure(core::ErrorCode::output,
                             "The output needs a new directory name and an existing parent");
    }
    // This check improves diagnostics only. Correctness depends on rename_exclusive, not this
    // check.
    const auto status = std::filesystem::symlink_status(target, error);
    if ((error && error != std::errc::no_such_file_or_directory) ||
        std::filesystem::exists(status)) {
        return core::failure(core::ErrorCode::output,
                             "The output path already exists or cannot be inspected");
    }
    for (unsigned attempt = 0; attempt < staging_attempts; ++attempt) {
        if (cancellation.requested(core::Checkpoint::staging)) {
            return core::cancelled();
        }
        stage.directory = parent / (target.filename().native() +
                                    utf8_path(".staging-" + std::to_string(attempt)).native());
        error.clear();
        if (private_directory(stage.directory, error)) {
            stage.owned = true;
            stage.owner = EntryLease::directory(stage.directory);
            if (!stage.owner.matches(stage.directory)) {
                return std::unexpected(
                    abandon(stage, {
                                       .code = core::ErrorCode::output,
                                       .message = "Cannot retain the created staging directory",
                                   }));
            }
            return {};
        }
        if (error && error != std::errc::file_exists) {
            return core::failure(core::ErrorCode::output,
                                 "Cannot create an exclusive staging directory");
        }
    }
    return core::failure(core::ErrorCode::output,
                         "All bounded staging names are occupied; existing paths were preserved");
}
namespace {
core::Result<std::optional<EntryIdentity>> owned_directory(Stage& stage,
                                                           const std::filesystem::path& path) {
    std::error_code error;
    if (private_directory(path, error)) {
        stage.created.insert(
            stage.created.begin(),
            {.path = path, .owner = EntryLease::directory(path), .directory = true});
    } else if (error && error != std::errc::file_exists) {
        return core::failure(core::ErrorCode::output,
                             "Cannot create a directory inside the bundle");
    }
    const auto found = std::ranges::find(stage.created, path, &OwnedEntry::path);
    if (found == stage.created.end() || !found->directory || !found->owner.matches(path)) {
        return core::failure(core::ErrorCode::output, "The bundle directory is not owned");
    }
    return found->owner.identity();
}
} // namespace
// The path a bundle file occupies inside staging. Directories are created as they are needed and
// recorded in the order they were made, so cleanup can undo exactly this invocation's work.
[[nodiscard]] core::Result<BundleSlot> place(Stage& stage, std::string_view relative) {
    if (!core::valid_bundle_path(relative)) {
        return core::failure(core::ErrorCode::argument, "A bundle needs a complete relative name");
    }
    if (!stage.owns_entries()) {
        return core::failure(core::ErrorCode::output, "Staging ownership changed before creation");
    }
    auto path = stage.directory;
    auto parent = stage.owner.identity();
    std::size_t start = 0;
    while (start < relative.size()) {
        if (stage.created.size() >= bundle_max_entries) {
            return core::failure(core::ErrorCode::resource,
                                 "Bundle staging exceeds its owned-entry limit");
        }
        const auto stop = std::min(relative.find('/', start), relative.size());
        const auto part = relative.substr(start, stop - start);
        path /= utf8_path(part);
        if (stop != relative.size()) {
            auto owned = owned_directory(stage, path);
            if (!owned) {
                return std::unexpected(owned.error());
            }
            parent = *owned;
        }
        start = stop + 1;
    }
    stage.created.insert(stage.created.begin(), {.path = path, .owner = {}});
    return BundleSlot{.path = path, .created = &stage.created.front().owner, .parent = parent};
}
} // namespace docenhance::io
