// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "bundle_stage.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "entry_identity.hpp"
#include "png_context.hpp"

#include <algorithm>
#include <cstddef>
#ifndef _WIN32
#include <expected>
#endif
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
namespace docenhance::io {
namespace {
constexpr unsigned staging_attempts = 64;
}
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
        if (std::filesystem::create_directory(stage.directory, error)) {
            stage.owned = true;
            stage.identity = entry_identity(stage.directory);
#ifndef _WIN32
            std::filesystem::permissions(stage.directory, std::filesystem::perms::owner_all,
                                         std::filesystem::perm_options::replace, error);
            if (error) {
                return std::unexpected(
                    abandon(stage, {
                                       .code = core::ErrorCode::output,
                                       .message = "Cannot make the staging directory private",
                                   }));
            }
#endif
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
// The path a bundle file occupies inside staging. Directories are created as they are needed and
// recorded in the order they were made, so cleanup can undo exactly this invocation's work.
[[nodiscard]] core::Result<std::filesystem::path> place(Stage& stage, std::string_view relative) {
    auto path = stage.directory;
    std::size_t start = 0;
    while (start < relative.size()) {
        const auto stop = std::min(relative.find('/', start), relative.size());
        const auto part = relative.substr(start, stop - start);
        if (part.empty() || part == "." || part == "..") {
            return core::failure(core::ErrorCode::invariant,
                                 "A bundle file must be named relative to the bundle");
        }
        path /= utf8_path(part);
        if (stop != relative.size()) {
            std::error_code error;
            if (std::filesystem::create_directory(path, error)) {
                stage.created.insert(
                    stage.created.begin(),
                    {.path = path, .identity = entry_identity(path), .directory = true});
            } else if (error || !std::ranges::any_of(stage.created, [&](const OwnedEntry& entry) {
                           return entry.directory && entry.path == path && entry.identity &&
                                  entry_identity(path) == entry.identity;
                       })) {
                return core::failure(core::ErrorCode::output,
                                     "Cannot create a directory inside the bundle");
            }
        }
        start = stop + 1;
    }
    stage.created.insert(stage.created.begin(), {.path = path, .identity = std::nullopt});
    return path;
}
} // namespace docenhance::io
