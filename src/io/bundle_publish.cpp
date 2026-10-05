// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "bundle_stage.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/artifact_limits.hpp"
#include "docenhance/io/publication.hpp"
#include "entry_identity.hpp"
#include "file_access.hpp"
#include "native_publication.hpp"

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
        const auto slot = place(stage, file.relative);
        if (!slot) {
            return std::unexpected(slot.error());
        }
        auto written = file.write(file.state, *slot);
        if (!written) {
            return std::unexpected(std::move(written.error()));
        }
    }
    return {};
}
core::Result<void> commit_bundle(Stage& stage, const std::filesystem::path& target,
                                 const core::Cancellation& cancellation, PublishRename commit,
                                 BundleValidation validation) {
    if (!stage.owns_entries()) {
        return std::unexpected(
            abandon(stage, {
                               .code = core::ErrorCode::output_verify,
                               .message = "Staging objects changed before commit",
                           }));
    }
    if (cancellation.requested(core::Checkpoint::commit)) {
        return std::unexpected(abandon(stage, core::cancelled().error()));
    }
    // A normal-execution cancellation observer can itself expose a namespace change. This is
    // ownership validation, not another stop observation or a change to the cancellation cutoff.
    if (!stage.owns_entries()) {
        return std::unexpected(
            abandon(stage, {
                               .code = core::ErrorCode::output_verify,
                               .message = "Staging objects changed at the commit cutoff",
                           }));
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
    stage.owned = !stage.committed;
    stage.retained = !stage.committed;
    const bool same_object = stage.owner.matches(target);
    stage.committed = stage.committed || same_object;
    stage.owned = !stage.committed;
    stage.retained = !stage.committed;
    if (!same_object) {
        return std::unexpected(stage.committed ? std::move(stage.integrity)
                                               : std::move(stage.uncertain));
    }
    // No cancellation after the cutoff: observations explain an authorized irreversible effect.
    const auto verified = validation.validate == nullptr
                              ? core::Result<void>{}
                              : validation.validate(validation.state, utf8_spelling(target), {});
    if (stage.owner.matches(target) && verified) {
        return {};
    }
    return std::unexpected(std::move(stage.integrity));
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
        const auto admitted = utf8_path(output_directory);
#ifdef _WIN32
        if (admitted.has_root_name() && !admitted.has_root_directory()) {
            return core::failure(core::ErrorCode::argument,
                                 "Drive-relative publication paths are unsupported");
        }
#endif
        const auto target =
            admitted.is_absolute() ? admitted : std::filesystem::current_path() / admitted;
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
        if (!stage.owns_entries()) {
            return std::unexpected(
                abandon(stage, {
                                   .code = core::ErrorCode::output_verify,
                                   .message = "Written bundle objects changed before validation",
                               }));
        }
        if (validation.validate != nullptr) {
            auto ready =
                validation.validate(validation.state, utf8_spelling(stage.directory), cancellation);
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
    if (!slot.valid_parent() || slot.created == nullptr) {
        return core::failure(core::ErrorCode::output, "The reserved file parent changed");
    }
    auto file = open_for_writing(slot.path);
    if (file == nullptr) {
        return core::failure(core::ErrorCode::output, "Cannot create a file inside the bundle");
    }
    *slot.created = EntryLease::capture(file.get(), slot.path);
    if (!slot.created->identity()) {
        return core::failure(core::ErrorCode::output, "Cannot retain the created bundle file");
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
core::Result<std::string> publish_bundle(const std::string& output_directory,
                                         std::span<const BundleFile> files,
                                         const core::Cancellation& cancellation,
                                         BundleValidation validation) {
    return publish_bundle(output_directory, files, cancellation, rename_exclusive, validation);
}
} // namespace docenhance::io
