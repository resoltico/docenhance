// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "png_context.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "entry_identity.hpp"
#include "file_access.hpp"
#include "native_publication.hpp"

#include <algorithm>
#include <csetjmp>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <png.h>
#include <pngconf.h>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace docenhance::io {
namespace {
// The jump target contains only trivial automatic state. All owning C++ objects are in its caller.
void read_bytes(png_structp png, png_bytep bytes, png_size_t count) noexcept {
    auto& input = *static_cast<PngInput*>(png_get_io_ptr(png));
    constexpr std::size_t transfer_bytes = std::size_t{64} * 1024;
    auto output = std::span{bytes, count};
    while (!output.empty()) {
        if (observe_cancellation(png, input.checkpoint)) {
            png_error(png, "Cancelled");
        }
        const auto chunk = output.first(std::min(transfer_bytes, output.size()));
        if (chunk.size() > input.remaining || !input.read(input.state, chunk)) {
            png_error(png, "The PNG input is truncated or exceeds its encoded-byte bound");
        }
        input.remaining -= chunk.size();
        output = output.subspan(chunk.size());
    }
}
[[nodiscard]] png_voidp allocate_block(PngMemory& memory, png_alloc_size_t size) {
    for (core::Buffer& slot : memory.blocks) {
        if (!slot.empty()) {
            continue;
        }
        auto allocated = memory.budget.get().allocate(size);
        if (!allocated) {
            return nullptr;
        }
        slot = std::move(*allocated);
        return slot.bytes().data();
    }
    return nullptr;
}
png_voidp allocate_png(png_structp png, png_alloc_size_t size) noexcept {
    auto& memory = *static_cast<PngMemory*>(png_get_mem_ptr(png));
    try {
        if (auto* const data = allocate_block(memory, size); data != nullptr) {
            return data;
        }
    } catch (...) {
        // No C++ exception may cross the C allocator callback.
        memory.exhausted = true;
        return nullptr;
    }
    memory.exhausted = true;
    return nullptr;
}
void free_png(png_structp png, png_voidp pointer) noexcept {
    auto& memory = *static_cast<PngMemory*>(png_get_mem_ptr(png));
    for (core::Buffer& block : memory.blocks) {
        if (block.bytes().data() == pointer) {
            block = core::Buffer{};
            return;
        }
    }
}
void fail_png(png_structp png, png_const_charp message) noexcept {
    auto& memory = *static_cast<PngMemory*>(png_get_error_ptr(png));
    const std::string_view text{message};
    const auto count = std::min(text.size(), memory.message.size() - 1);
    std::ranges::copy(std::views::take(text, static_cast<std::ptrdiff_t>(count)),
                      memory.message.begin());
    // Invoke the CRT directly; jump storage and every C++ owner belong to the caller.
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    std::longjmp(std::begin(memory.jump), 1);
}
void warn_png(png_structp png, png_const_charp message) noexcept {
    // Corrupt/truncated metadata is not silently accepted as a successfully decoded document.
    fail_png(png, message);
}
void initialize_png(PngContext& context) {
    // The constructor owns all resources outside this trivial native bootstrap frame.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(std::begin(context.memory.jump)) != 0) {
        context.memory.exhausted = true;
        return;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    auto* const memory = &context.memory;
    context.png = context.writing
                      ? png_create_write_struct_2(PNG_LIBPNG_VER_STRING, memory, fail_png, warn_png,
                                                  memory, allocate_png, free_png)
                      : png_create_read_struct_2(PNG_LIBPNG_VER_STRING, memory, fail_png, warn_png,
                                                 memory, allocate_png, free_png);
    if (context.png != nullptr) {
        context.info = png_create_info_struct(context.png);
    }
}
} // namespace

PngContext::PngContext(core::Budget& budget, bool write, const core::Cancellation& cancellation)
    : memory(budget, cancellation), writing(write) {
    initialize_png(*this);
}
PngContext::~PngContext() {
    if (png != nullptr) {
        if (writing) {
            png_destroy_write_struct(&png, &info);
        } else {
            png_destroy_read_struct(&png, &info, nullptr);
        }
    }
}
bool PngContext::create(const BundleSlot& slot) {
    if (png == nullptr || info == nullptr) {
        memory.exhausted = true;
        return false;
    }
    if (!writing || !slot.valid_parent() || slot.created == nullptr) {
        return false;
    }
    file = open_for_writing(slot.path);
    if (file != nullptr) {
        *slot.created = EntryLease::capture(file.get(), slot.path);
    }
    return file != nullptr && slot.created->identity().has_value();
}
bool PngContext::open_reading(const std::filesystem::path& path) {
    if (writing || png == nullptr || info == nullptr) {
        return false;
    }
    file = open_for_reading(path);
    return file != nullptr;
}
bool PngContext::close_output() noexcept {
    std::FILE* const closing = file.release();
    if (closing == nullptr) {
        return false;
    }
    const bool flushed = std::fflush(closing) == 0;
    const bool closed = std::fclose(closing) == 0; // NOLINT(cppcoreguidelines-owning-memory)
    return flushed && closed;
}
core::Result<void> PngContext::finish_output(core::Result<void> result) {
    if (file == nullptr) {
        return result;
    }
    const bool closed = close_output();
    if (!closed && (result || result.error().code == core::ErrorCode::cancelled)) {
        return core::failure(core::ErrorCode::output, "Cannot finish writing the PNG output");
    }
    return result;
}
core::Error PngContext::error(core::ErrorCode fallback) const {
    if (memory.exhausted) {
        return {
            .code = core::ErrorCode::resource,
            .message = "The PNG codec exhausted its bounded allocation budget",
        };
    }
    if (memory.cancelled) {
        return core::cancelled().error();
    }
    return {
        .code = fallback,
        .message = memory.message.front() == '\0' ? "Cannot open or initialize the PNG stream"
                                                  : memory.message.data(),
    };
}
void install_png_reader(const PngContext& context, PngInput& input) {
    png_set_read_fn(context.png, &input, read_bytes);
}
bool observe_cancellation(png_structp png, core::Checkpoint at) noexcept {
    auto& memory = *static_cast<PngMemory*>(png_get_error_ptr(png));
    if (memory.cancellation.requested(at)) {
        memory.cancelled = true;
    }
    return memory.cancelled;
}
} // namespace docenhance::io
