// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/bundle.hpp"
#include "png_context.hpp"
#include "publication.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace docenhance::io {
namespace {
constexpr unsigned staging_attempts = 64;
struct Stage {
    std::filesystem::path directory;
    // Everything this invocation created, newest first, so cleanup removes only its own work.
    std::vector<std::filesystem::path> created;
    bool owned = false;
    Stage() = default;
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;
    Stage(Stage&&) = delete;
    Stage& operator=(Stage&&) = delete;
    // Never recursively delete: a foreign entry is evidence that cleanup cannot be guaranteed.
    [[nodiscard]] bool cleanup() noexcept {
        if (!owned) {
            return true;
        }
        try {
            bool failed = false;
            for (const auto& path : created) {
                std::error_code error;
                std::filesystem::remove(path, error);
                failed = failed || static_cast<bool>(error);
            }
            std::error_code directory_error;
            std::filesystem::remove(directory, directory_error);
            owned = failed || static_cast<bool>(directory_error);
            return !owned;
        } catch (...) {
            // Even allocation failure in a filesystem error-code overload cannot escape cleanup.
            return false;
        }
    }
    ~Stage() {
        static_cast<void>(cleanup());
    }
};
[[nodiscard]] core::Error abandon(Stage& stage, core::Error error) {
    error.publication =
        stage.owned ? core::Publication::not_published : core::Publication::not_started;
    if (!stage.cleanup()) {
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
                stage.created.insert(stage.created.begin(), path);
            } else if (error) {
                return core::failure(core::ErrorCode::output,
                                     "Cannot create a directory inside the bundle");
            }
        }
        start = stop + 1;
    }
    stage.created.insert(stage.created.begin(), path);
    return path;
}
} // namespace

core::Result<std::string> publish_bundle(const std::string& output_directory,
                                         std::span<const BundleFile> files,
                                         const core::Cancellation& cancellation,
                                         PublishRename commit) {
    if (files.empty() || commit == nullptr) {
        return core::failure(core::ErrorCode::argument, "A bundle needs at least one file");
    }
    Stage stage;
    try {
        const auto target = utf8_path(output_directory);
        // All potentially allocating return metadata is prepared before the commit point.
#ifdef _WIN32
        const auto separator_index = output_directory.find_last_of("/\\");
        const char separator =
            separator_index == std::string::npos ? '\\' : output_directory.at(separator_index);
#else
        // Backslash is a filename byte on POSIX, not a directory separator.
        constexpr char separator = '/';
#endif
        std::string published = output_directory + separator + std::string(files.front().relative);
        auto reserved = reserve_stage(stage, target, cancellation);
        if (!reserved) {
            return std::unexpected(std::move(reserved.error()));
        }
        // Written in the order the caller declared: a file that describes the others, such as the
        // run record, can only be written once those exist.
        for (const auto& file : files) {
            auto path = place(stage, file.relative);
            if (!path) {
                return std::unexpected(abandon(stage, std::move(path.error())));
            }
            const BundleSlot slot{.path = std::move(*path)};
            auto written = file.write(file.state, slot);
            if (!written) {
                return std::unexpected(abandon(stage, std::move(written.error())));
            }
        }
        // This snapshot is the cancellation cutoff. Once it authorizes commit, report only
        // the native operation's real outcome; a late stop must never erase a completed bundle.
        if (cancellation.requested(core::Checkpoint::commit)) {
            return std::unexpected(abandon(stage, core::cancelled().error()));
        }
        const auto error = commit(stage.directory, target);
        if (error) {
            auto failure = abandon(stage, {
                                              .code = core::ErrorCode::output,
                                              .message = "The exclusive publication was refused",
                                          });
            if (!definitely_not_published(error)) {
                failure.code = core::ErrorCode::publication_unknown;
                failure.publication = core::Publication::unknown;
                failure.message = "The filesystem could not confirm whether publication committed; "
                                  "inspect the output";
            }
            return std::unexpected(std::move(failure));
        }
        stage.owned = false;
        return core::Result<std::string>{std::move(published)};
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            abandon(stage, {
                               .code = core::ErrorCode::resource,
                               .message = "Publication exhausted a metadata allocation",
                           }));
    } catch (const std::filesystem::filesystem_error&) {
        return std::unexpected(
            abandon(stage, {
                               .code = core::ErrorCode::output,
                               .message = "A filesystem operation prevented publication",
                           }));
    }
}

core::Result<void> write_bytes(const BundleSlot& slot, std::string_view content) {
    const auto file = open_for_writing(slot.path);
    if (file == nullptr) {
        return core::failure(core::ErrorCode::output, "Cannot create a file inside the bundle");
    }
    if (!content.empty() &&
        std::fwrite(content.data(), 1, content.size(), file.get()) != content.size()) {
        return core::failure(core::ErrorCode::output, "A bundle file could not be written");
    }
    return std::fflush(file.get()) == 0
               ? core::Result<void>{}
               : core::failure(core::ErrorCode::output, "A bundle file could not be completed");
}
std::string file_name(const std::string& path) {
    const auto name = utf8_path(path).filename().string();
    return name.empty() ? path : name;
}
core::Result<std::string> publish_bundle(const std::string& output_directory,
                                         std::span<const BundleFile> files,
                                         const core::Cancellation& cancellation) {
    return publish_bundle(output_directory, files, cancellation, rename_exclusive);
}
core::Result<std::string> publish_generated_png(const std::string& output_directory,
                                                PngWriterRef writer,
                                                const core::Cancellation& cancellation,
                                                PublishRename commit) {
    if (writer.write == nullptr || writer.state == nullptr) {
        return core::failure(core::ErrorCode::argument, "Cannot publish an empty image");
    }
    // One image is a bundle of one file, through the same transaction.
    const std::array<BundleFile, 1> files{
        BundleFile{
            .relative = "result.png",
            .state = writer.state,
            .write = writer.write,
        },
    };
    return publish_bundle(output_directory, files, cancellation, commit);
}
} // namespace docenhance::io
