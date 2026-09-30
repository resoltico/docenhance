// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "bundle_stage.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/bundle.hpp"
#include "entry_identity.hpp"
#include "png_context.hpp"
#include "publication.hpp"

#include <array>
#include <expected>
#include <filesystem>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace docenhance::io {

namespace {
core::Result<void> write_files(Stage& stage, std::span<const BundleFile> files) {
    for (const auto& file : files) {
        const auto path = place(stage, file.relative);
        if (!path) {
            return std::unexpected(path.error());
        }
        const BundleSlot slot{.path = *path, .created = &stage.created.front().identity};
        auto written = file.write(file.state, slot);
        if (!written) {
            return std::unexpected(std::move(written.error()));
        }
    }
    return {};
}
core::Result<void> commit_bundle(Stage& stage, const std::filesystem::path& target,
                                 const core::Cancellation& cancellation, PublishRename commit,
                                 BundleValidation validation) {
    if (cancellation.requested(core::Checkpoint::commit)) {
        return std::unexpected(abandon(stage, core::cancelled().error()));
    }
    const auto error = commit(stage.directory, target);
    if (error && definitely_not_published(error)) {
        return std::unexpected(
            abandon(stage, {
                               .code = core::ErrorCode::output,
                               .message = "The exclusive publication was refused",
                           }));
    }
    stage.committed = !error;
    stage.owned = static_cast<bool>(error);
    stage.retained = static_cast<bool>(error);
    // No cancellation after the cutoff: observations explain an authorized irreversible effect.
    const auto observed = validation.observe == nullptr
                              ? BundleObservation::unobservable
                              : validation.observe(validation.state, utf8_spelling(target));
    if (observed == BundleObservation::consistent ||
        (stage.committed && validation.observe == nullptr)) {
        return {};
    }
    if (stage.committed || observed == BundleObservation::integrity_failure) {
        return std::unexpected(std::move(stage.integrity));
    }
    return std::unexpected(std::move(stage.uncertain));
}
core::Error contain_failure(Stage& stage, core::ErrorCode code) {
    if (stage.committed) {
        return std::move(stage.integrity);
    }
    if (stage.retained) {
        return std::move(stage.uncertain);
    }
    return abandon(stage, {.code = code, .message = "A publication operation failed"});
}
} // namespace
core::Result<std::string> publish_bundle(const std::string& output_directory,
                                         std::span<const BundleFile> files,
                                         const core::Cancellation& cancellation,
                                         PublishRename commit, BundleValidation validation) {
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
        auto written = write_files(stage, files);
        if (!written) {
            return std::unexpected(abandon(stage, std::move(written.error())));
        }
        if (validation.prepare != nullptr) {
            auto ready =
                validation.prepare(validation.state, utf8_spelling(stage.directory), cancellation);
            if (!ready) {
                return std::unexpected(abandon(stage, std::move(ready.error())));
            }
        }
        auto committed = commit_bundle(stage, target, cancellation, commit, validation);
        if (!committed) {
            return std::unexpected(std::move(committed.error()));
        }
        return core::Result<std::string>{std::move(published)};
    } catch (const std::bad_alloc&) {
        return std::unexpected(contain_failure(stage, core::ErrorCode::resource));
    } catch (...) {
        return std::unexpected(contain_failure(stage, core::ErrorCode::output));
    }
}

core::Result<void> write_bytes(const BundleSlot& slot, std::string_view content) {
    return write_bytes(slot, content, {});
}
core::Result<void> write_bytes(const BundleSlot& slot, std::string_view content,
                               BundleStream operations) {
    auto file = open_for_writing(slot.path);
    if (file == nullptr) {
        return core::failure(core::ErrorCode::output, "Cannot create a file inside the bundle");
    }
    if (slot.created != nullptr) {
        *slot.created = stream_identity(file.get());
    }
    if (!content.empty() &&
        operations.write(content.data(), 1, content.size(), file.get()) != content.size()) {
        return core::failure(core::ErrorCode::output, "A bundle file could not be written");
    }
    const bool flushed = operations.flush(file.get()) == 0;
    const bool closed = operations.close(file.release()) == 0;
    return flushed && closed
               ? core::Result<void>{}
               : core::failure(core::ErrorCode::output, "A bundle file could not be completed");
}
std::string file_name(const std::string& path) {
    const auto name = utf8_spelling(utf8_path(path).filename());
    return name.empty() ? path : name;
}
core::Result<std::string> publish_bundle(const std::string& output_directory,
                                         std::span<const BundleFile> files,
                                         const core::Cancellation& cancellation,
                                         BundleValidation validation) {
    return publish_bundle(output_directory, files, cancellation, rename_exclusive, validation);
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
