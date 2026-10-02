// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "bundle_stage.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/bundle.hpp"
#include "entry_identity.hpp"
#include "png_context.hpp"
#include "publication.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace docenhance::io {

namespace {
core::Result<void> write_files(Stage& stage, std::span<const BundleFile> files,
                               const core::Cancellation& cancellation) {
    for (const auto& file : files) {
        if (cancellation.requested(core::Checkpoint::staging)) {
            return core::cancelled();
        }
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
    if (files.empty() || files.size() > bundle_max_entries || commit == nullptr ||
        std::ranges::any_of(files, [](const auto& file) { return file.write == nullptr; })) {
        return core::failure(core::ErrorCode::argument,
                             "A bundle needs a bounded nonempty file table with valid writers");
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
        auto written = write_files(stage, files, cancellation);
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

namespace {
core::Result<void> write_content(std::FILE* file, std::string_view content,
                                 const core::Cancellation& cancellation, BundleStream operations) {
    constexpr std::size_t transfer = std::size_t{64} * 1024;
    while (!content.empty()) {
        if (cancellation.requested(core::Checkpoint::encode)) {
            return core::cancelled();
        }
        const auto part = content.substr(0, std::min(transfer, content.size()));
        if (operations.write(part.data(), 1, part.size(), file) != part.size()) {
            return core::failure(core::ErrorCode::output, "A bundle file could not be written");
        }
        content.remove_prefix(part.size());
    }
    return {};
}
} // namespace
core::Result<void> write_bytes(const BundleSlot& slot, std::string_view content,
                               const core::Cancellation& cancellation) {
    return write_bytes(slot, content, cancellation, {});
}
core::Result<void> write_bytes(const BundleSlot& slot, std::string_view content,
                               const core::Cancellation& cancellation, BundleStream operations) {
    if (cancellation.requested(core::Checkpoint::encode)) {
        return core::cancelled();
    }
    auto file = open_for_writing(slot.path);
    if (file == nullptr) {
        return core::failure(core::ErrorCode::output, "Cannot create a file inside the bundle");
    }
    if (slot.created != nullptr) {
        *slot.created = stream_identity(file.get());
    }
    auto written = write_content(file.get(), content, cancellation, operations);
    const bool flushed = operations.flush(file.get()) == 0;
    const bool closed = operations.close(file.release()) == 0;
    // A real write failure wins; delayed stream errors also outrank cooperative cancellation.
    if (!written && written.error().code != core::ErrorCode::cancelled) {
        return written;
    }
    if (!flushed || !closed) {
        return core::failure(core::ErrorCode::output, "A bundle file could not be completed");
    }
    return written;
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
} // namespace docenhance::io
