// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "png_context.hpp"
#include "publication.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <new>
#include <string>
#include <system_error>
#include <utility>

namespace docenhance::io {
namespace {
constexpr unsigned staging_attempts = 64;
struct Stage {
    std::filesystem::path directory;
    std::filesystem::path output;
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
            std::error_code file_error;
            std::error_code directory_error;
            std::filesystem::remove(output, file_error);
            std::filesystem::remove(directory, directory_error);
            owned = file_error || directory_error;
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
        stage.output = stage.directory / "result.png";
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
} // namespace

core::Result<std::string> publish_generated_png(const std::string& output_directory,
                                                PngWriterRef writer,
                                                const core::Cancellation& cancellation,
                                                PublishRename commit) {
    if (writer.write == nullptr || writer.state == nullptr || commit == nullptr) {
        return core::failure(core::ErrorCode::argument, "Cannot publish an empty image");
    }
    Stage stage;
    try {
        const auto target = utf8_path(output_directory);
        // All potentially allocating return metadata is prepared before the commit point.
        // Preserve the admitted UTF-8 spelling rather than round-tripping through native
        // character types, including its preferred separator when it has one.
#ifdef _WIN32
        const auto separator_index = output_directory.find_last_of("/\\");
        const char separator =
            separator_index == std::string::npos ? '\\' : output_directory.at(separator_index);
#else
        // Backslash is a filename byte on POSIX, not a directory separator.
        constexpr char separator = '/';
#endif
        std::string published = output_directory + separator + "result.png";
        auto reserved = reserve_stage(stage, target, cancellation);
        if (!reserved) {
            return std::unexpected(std::move(reserved.error()));
        }
        auto encoded = writer.write(writer.state, stage.output);
        if (!encoded) {
            return std::unexpected(abandon(stage, std::move(encoded.error())));
        }
        // This snapshot is the cancellation cutoff. Once it authorizes commit, report only
        // the native operation's real outcome; a late stop must never erase a completed image.
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
core::Result<std::string> publish_png(const std::string& output_directory,
                                      image::PlaneView<const std::uint8_t> image,
                                      core::Budget& budget, const core::Cancellation& cancellation,
                                      PublishRename commit) {
    struct BinaryWriter {
        image::PlaneView<const std::uint8_t> image;
        std::reference_wrapper<core::Budget> budget;
        std::reference_wrapper<const core::Cancellation> cancellation;
    };
    if (image.empty()) {
        return core::failure(core::ErrorCode::argument, "Cannot publish an empty image");
    }
    BinaryWriter state{.image = image, .budget = budget, .cancellation = cancellation};
    const PngWriterRef writer{
        .state = &state,
        .write =
            [](void* raw, const std::filesystem::path& path) {
                auto const& value = *static_cast<BinaryWriter*>(raw);
                return encode_png(path, value.image, value.budget.get(), value.cancellation.get());
            },
    };
    return publish_generated_png(output_directory, writer, cancellation, commit);
}
core::Result<std::string> publish_grayscale_png(const std::string& output_directory,
                                                image::PlaneView<const std::uint8_t> image,
                                                core::Budget& budget,
                                                const core::Cancellation& cancellation) {
    return publish_png(output_directory, image, budget, cancellation, rename_exclusive);
}
} // namespace docenhance::io
