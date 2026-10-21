// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "bundle_native.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/bundle_snapshot.hpp"
#include "docenhance/fs/digest.hpp"
#include "file_access.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <span>
#include <string>
#include <utility>
namespace docenhance::fs {
namespace {
core::Result<core::Buffer> snapshot(FileHandle file, std::size_t limit, core::Budget& budget,
                                    const core::Cancellation& cancellation) {
    if (std::fseek(file.get(), 0, SEEK_END) != 0) {
        return core::failure(core::ErrorCode::input, "Cannot inspect bundle file size");
    }
    const auto length = std::ftell(file.get());
    if (length < 0 || std::cmp_greater(length, limit) || std::fseek(file.get(), 0, SEEK_SET) != 0) {
        return core::failure(core::ErrorCode::input, "Bundle file exceeds its byte bound");
    }
    auto bytes = budget.allocate(static_cast<std::size_t>(length));
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto remaining = bytes->bytes();
    constexpr std::size_t transfer = std::size_t{64} * 1024;
    while (!remaining.empty()) {
        if (cancellation.requested(core::Checkpoint::verification)) {
            return core::cancelled();
        }
        const auto part = remaining.first(std::min(transfer, remaining.size()));
        if (std::fread(part.data(), 1, part.size(), file.get()) != part.size()) {
            return core::failure(core::ErrorCode::input,
                                 "Bundle file changed size or could not be read");
        }
        remaining = remaining.subspan(part.size());
    }
    if (std::fgetc(file.get()) != EOF || std::ferror(file.get()) != 0) {
        return core::failure(core::ErrorCode::input,
                             "Bundle file changed size or could not be finished");
    }
    return bytes;
}
core::Result<core::Buffer> read(const BundleDirectory& directory, const std::string& name,
                                std::size_t limit, core::Budget& budget,
                                const core::Cancellation& cancellation) {
    if (cancellation.requested(core::Checkpoint::verification)) {
        return core::cancelled();
    }
    auto file = directory.file(name);
    if (!file) {
        return std::unexpected(file.error());
    }
    return snapshot(std::move(*file), limit, budget, cancellation);
}
core::Result<void> append(BundleSnapshot& result, const std::string& name, core::Buffer bytes,
                          const core::Cancellation& cancellation) {
    auto identity = identify(bytes.bytes(), cancellation);
    if (!identity) {
        return std::unexpected(identity.error());
    }
    result.contents.files.push_back({.name = name, .identity = std::move(*identity)});
    if (name == "run.json") {
        result.record = std::move(bytes);
    } else if (name == "result.png") {
        result.image = std::move(bytes);
    } else if (name == "assets/protect-mask.png") {
        result.mask = std::move(bytes);
    } else if (name == "assets/psf.png") {
        result.psf = std::move(bytes);
    } else {
        return core::failure(core::ErrorCode::invariant, "Unadmitted bundle snapshot role");
    }
    return {};
}
core::Result<void> read_assets(const BundleDirectory& root, BundleSnapshot& result,
                               core::Budget& budget, const core::Cancellation& cancellation) {
    auto assets = root.child("assets");
    if (!assets) {
        return std::unexpected(assets.error());
    }
    auto names = assets->entries();
    if (!names) {
        return std::unexpected(std::move(names.error()));
    }
    if (names->empty() || names->size() > 2 || std::ranges::any_of(*names, [](const auto& name) {
            return name != "protect-mask.png" && name != "psf.png";
        })) {
        return core::failure(core::ErrorCode::input, "Bundle assets inventory is not closed");
    }
    for (const auto& name : *names) {
        const auto limit =
            name == "psf.png" ? core::psf_encoded_bytes_max : core::bundle_max_file_bytes;
        auto bytes = read(*assets, name, limit, budget, cancellation);
        if (!bytes) {
            return std::unexpected(std::move(bytes.error()));
        }
        if (auto stored = append(result, "assets/" + name, std::move(*bytes), cancellation);
            !stored) {
            return stored;
        }
    }
    auto after = assets->entries();
    if (!after) {
        return std::unexpected(std::move(after.error()));
    }
    std::ranges::sort(*names);
    std::ranges::sort(*after);
    if (*after != *names) {
        return core::failure(core::ErrorCode::input,
                             "Bundle assets changed during snapshot acquisition");
    }
    result.contents.directories.emplace_back("assets");
    return {};
}
} // namespace
core::Result<BundleSnapshot> read_bundle(const std::string& directory, core::Budget& budget,
                                         const core::Cancellation& cancellation,
                                         std::size_t record_limit) {
    auto root = BundleDirectory::open(utf8_path(directory));
    if (!root) {
        return std::unexpected(root.error());
    }
    auto names = root->entries();
    if (!names) {
        return std::unexpected(names.error());
    }
    BundleSnapshot result;
    for (const auto& name : *names) {
        if (name == "assets") {
            auto assets = read_assets(*root, result, budget, cancellation);
            if (!assets) {
                return std::unexpected(std::move(assets.error()));
            }
            continue;
        }
        if (name != "run.json" && name != "result.png") {
            return core::failure(core::ErrorCode::input, "Bundle contains an undeclared entry");
        }
        const auto limit = name == "run.json" ? std::min(record_limit, core::bundle_max_file_bytes)
                                              : core::bundle_max_file_bytes;
        auto bytes = read(*root, name, limit, budget, cancellation);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        auto stored = append(result, name, std::move(*bytes), cancellation);
        if (!stored) {
            return std::unexpected(stored.error());
        }
    }
    auto after = root->entries();
    if (!after) {
        return std::unexpected(after.error());
    }
    std::ranges::sort(*names);
    std::ranges::sort(*after);
    if (*names != *after) {
        return core::failure(core::ErrorCode::input,
                             "Bundle inventory changed during snapshot acquisition");
    }
    return result;
}
} // namespace docenhance::fs
