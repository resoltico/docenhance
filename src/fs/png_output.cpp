// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/png_stream.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "entry_identity.hpp"
#include "file_access.hpp"
#include "native_publication.hpp"
#include "png_file_rows.hpp"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <span>
namespace docenhance::fs {
namespace {
struct PngFile {
    std::reference_wrapper<const BundleSlot> slot;
    FileHandle file;
};
bool open_png(void* const state) {
    auto& owner = *static_cast<PngFile*>(state);
    const auto& slot = owner.slot.get();
    if (!slot.valid_parent() || slot.created == nullptr) {
        return false;
    }
    owner.file = open_for_writing(slot.path);
    if (owner.file != nullptr) {
        *slot.created = EntryLease::capture(owner.file.get(), slot.path);
    }
    return owner.file != nullptr && slot.created->identity().has_value();
}
bool write_png(void* const state, std::span<const std::uint8_t> bytes) noexcept {
    const auto& owner = *static_cast<PngFile*>(state);
    return std::fwrite(bytes.data(), 1, bytes.size(), owner.file.get()) == bytes.size();
}
bool flush_png(void* const state) noexcept {
    const auto& owner = *static_cast<PngFile*>(state);
    return std::fflush(owner.file.get()) == 0;
}
core::Result<void> finish_png(PngFile& owner, core::Result<void> result) {
    std::FILE* const closing = owner.file.release();
    if (closing == nullptr) {
        return result;
    }
    const bool flushed = std::fflush(closing) == 0;
    const bool closed = std::fclose(closing) == 0; // NOLINT(cppcoreguidelines-owning-memory)
    if ((!flushed || !closed) && (result || result.error().code == core::ErrorCode::cancelled)) {
        return core::failure(core::ErrorCode::output, "Cannot finish writing the PNG output");
    }
    return result;
}
codec::PngOutput output_stream(PngFile& owner) {
    return {.state = &owner, .open = open_png, .write = write_png, .flush = flush_png};
}
} // namespace
core::Result<void> encode_png(const BundleSlot& slot, image::PlaneView<const std::uint8_t> view,
                              core::Budget& budget, const core::Cancellation& cancellation) {
    PngFile owner{.slot = slot, .file = {}};
    return finish_png(owner, codec::encode_png(output_stream(owner), view, budget, cancellation));
}
core::Result<void> encode_png_rows(const BundleSlot& slot, image::RowSource& source,
                                   core::Budget& budget, const core::Cancellation& cancellation) {
    PngFile owner{.slot = slot, .file = {}};
    return finish_png(owner,
                      codec::encode_png_rows(output_stream(owner), source, budget, cancellation));
}
} // namespace docenhance::fs
