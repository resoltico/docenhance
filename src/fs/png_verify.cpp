// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/png_stream.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "entry_identity.hpp"
#include "file_access.hpp"
#include "png_file_rows.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <span>
namespace docenhance::fs {
namespace {
struct PngFile {
    std::reference_wrapper<const std::filesystem::path> path;
    const EntryLease* owner = nullptr;
    FileHandle file;
};
core::Result<void> open_png(void* const state) {
    auto& input = *static_cast<PngFile*>(state);
    input.file = open_for_reading(input.path.get());
    if (input.file == nullptr) {
        return core::failure(core::ErrorCode::output_verify,
                             "Cannot open or initialize the PNG stream");
    }
    if (input.owner != nullptr &&
        (!input.owner->identity() || stream_identity(input.file.get()) != input.owner->identity() ||
         !input.owner->matches(input.path.get()))) {
        return core::failure(core::ErrorCode::output_verify, "The written PNG object changed");
    }
    return {};
}
bool read_png(void* const state, std::span<std::uint8_t> bytes) noexcept {
    const auto& input = *static_cast<PngFile*>(state);
    return std::fread(bytes.data(), 1, bytes.size(), input.file.get()) == bytes.size();
}
bool skip_png(void* const state, std::uint32_t count) noexcept {
    const auto& input = *static_cast<PngFile*>(state);
    return std::fseek(input.file.get(), static_cast<std::int32_t>(count), SEEK_CUR) == 0;
}
bool finish_inventory(void* const state) noexcept {
    const auto& input = *static_cast<PngFile*>(state);
    return std::fgetc(input.file.get()) == EOF && std::ferror(input.file.get()) == 0 &&
           std::fseek(input.file.get(), 0, SEEK_SET) == 0;
}
} // namespace
core::Result<void> verify_png_rows(const std::filesystem::path& path, image::RowSource& source,
                                   core::Budget& budget, const core::Cancellation& cancellation,
                                   const EntryLease* const owner) {
    PngFile input{.path = path, .owner = owner, .file = {}};
    const codec::PngReadStream stream{
        .input =
            {
                .state = &input,
                .read = read_png,
                .remaining = core::bundle_max_file_bytes,
                .checkpoint = core::Checkpoint::verification,
            },
        .open = open_png,
        .skip = skip_png,
        .finish_inventory = finish_inventory,
    };
    return codec::verify_png_rows(stream, source, budget, cancellation);
}
core::Result<void> verify_png_image(const std::filesystem::path& path,
                                    image::PlaneView<const std::uint8_t> image,
                                    core::Budget& budget, const core::Cancellation& cancellation,
                                    const EntryLease* const owner) {
    image::PlaneRows rows{image};
    return verify_png_rows(path, rows, budget, cancellation, owner);
}
} // namespace docenhance::fs
