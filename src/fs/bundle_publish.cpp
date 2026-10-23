// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "bundle_stage.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/artifact_limits.hpp"
#include "docenhance/fs/publication.hpp"
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
#include <type_traits>
#include <utility>

namespace docenhance::fs {

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
        auto written = write_bytes(*slot, file.bytes, cancellation);
        if (!written) {
            return std::unexpected(std::move(written.error()));
        }
    }
    return {};
}
PublishResult<void> commit_bundle(Stage& stage, const std::filesystem::path& target,
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
    auto verified = validation.validate == nullptr
                        ? core::Result<void>{}
                        : validation.validate(validation.state, utf8_spelling(target), {});
    const bool owns_target = stage.owner.matches(target);
    if (owns_target && verified) {
        return {};
    }
    if (owns_target && !verified && !verified.error().message.empty()) {
        static_assert(std::is_nothrow_move_assignable_v<std::string>);
        stage.integrity.error.message = std::move(verified.error().message);
    }
    return std::unexpected(std::move(stage.integrity));
}
PublishFailure contain_failure(Stage& stage, core::ErrorCode code) noexcept {
    if (stage.committed) {
        return std::move(stage.integrity);
    }
    if (stage.retained) {
        return std::move(stage.uncertain);
    }
    stage.failure.code = code;
    return abandon(stage, std::move(stage.failure));
}
} // namespace
PublishResult<std::string> publish_bundle(const std::string& output_directory,
                                          std::span<const BundleFile> files,
                                          const core::Cancellation& cancellation,
                                          PublishRename commit, BundleValidation validation) {
    if (files.empty() || files.size() > bundle_max_entries || commit == nullptr ||
        std::ranges::any_of(files, [](const auto& file) {
            return file.bytes.size() > core::bundle_max_file_bytes;
        })) {
        return std::unexpected(PublishFailure{
            .error =
                {
                    .code = core::ErrorCode::argument,
                    .message = "A bundle needs a bounded nonempty byte table and commit operation",
                },
            .publication = core::Publication::not_started,
        });
    }
    static_assert(std::is_nothrow_move_constructible_v<PublishFailure>);
    Stage stage;
    try {
        const auto admitted = utf8_path(output_directory);
#ifdef _WIN32
        if (admitted.has_root_name() && !admitted.has_root_directory()) {
            return std::unexpected(PublishFailure{
                .error =
                    {
                        .code = core::ErrorCode::argument,
                        .message = "Drive-relative publication paths are unsupported",
                    },
                .publication = core::Publication::not_started,
            });
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
            return std::unexpected(abandon(stage, std::move(reserved.error())));
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
        return PublishResult<std::string>{std::move(published)};
    } catch (const std::bad_alloc&) {
        return std::unexpected(contain_failure(stage, core::ErrorCode::resource));
    } catch (...) {
        return std::unexpected(contain_failure(stage, core::ErrorCode::output));
    }
}

namespace {
core::Result<void> write_content(std::FILE* file, std::span<const std::byte> content,
                                 const core::Cancellation& cancellation, BundleStream operations) {
    constexpr std::size_t transfer = std::size_t{64} * 1024;
    while (!content.empty()) {
        if (cancellation.requested(core::Checkpoint::encode)) {
            return core::cancelled();
        }
        const auto part = content.first(std::min(transfer, content.size()));
        if (operations.write(part.data(), 1, part.size(), file) != part.size()) {
            return core::failure(core::ErrorCode::output, "A bundle file could not be written");
        }
        content = content.subspan(part.size());
    }
    return {};
}
} // namespace
core::Result<void> write_bytes(const BundleSlot& slot, std::span<const std::byte> content,
                               const core::Cancellation& cancellation) {
    return write_bytes(slot, content, cancellation, {});
}
core::Result<void> write_bytes(const BundleSlot& slot, std::span<const std::byte> content,
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
PublishResult<std::string> publish_bundle(const std::string& output_directory,
                                          std::span<const BundleFile> files,
                                          const core::Cancellation& cancellation,
                                          BundleValidation validation) {
    return publish_bundle(output_directory, files, cancellation, rename_exclusive, validation);
}
} // namespace docenhance::fs
