// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "png_context.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <png.h>
#include <pngconf.h>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace docenhance::io {
namespace {
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
    png_longjmp(png, 1);
}
void warn_png(png_structp png, png_const_charp message) noexcept {
    // Corrupt/truncated metadata is not silently accepted as a successfully decoded document.
    fail_png(png, message);
}
} // namespace

PngContext::PngContext(core::Budget& budget, bool write, const core::Cancellation& cancellation)
    : memory(budget, cancellation), writing(write) {
    png = writing ? png_create_write_struct_2(PNG_LIBPNG_VER_STRING, &memory, fail_png, warn_png,
                                              &memory, allocate_png, free_png)
                  : png_create_read_struct_2(PNG_LIBPNG_VER_STRING, &memory, fail_png, warn_png,
                                             &memory, allocate_png, free_png);
    if (png != nullptr) {
        info = png_create_info_struct(png);
    }
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
void FileCloser::operator()(std::FILE* file) const noexcept {
    // Best-effort cleanup. Success is possible only after close_output explicitly checks fclose.
    static_cast<void>(std::fclose(file)); // NOLINT(cppcoreguidelines-owning-memory)
}
bool PngContext::open(const std::filesystem::path& path) {
    if (png == nullptr || info == nullptr) {
        memory.exhausted = true;
        return false;
    }
#ifdef _WIN32
    std::FILE* opened = nullptr;
    // NOLINTNEXTLINE(misc-include-cleaner): MSVC exposes _wfopen_s through C runtime internals.
    if (_wfopen_s(&opened, path.c_str(), writing ? L"wbx" : L"rb") != 0) {
        return false;
    }
    file.reset(opened);
#else
    // The unique_ptr immediately takes ownership of the C stream.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    file.reset(std::fopen(path.c_str(), writing ? "wbx" : "rb"));
#endif
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
bool observe_cancellation(png_structp png, core::Checkpoint at) noexcept {
    auto& memory = *static_cast<PngMemory*>(png_get_error_ptr(png));
    if (memory.cancellation.requested(at)) {
        memory.cancelled = true;
    }
    return memory.cancelled;
}
std::filesystem::path utf8_path(std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const char byte : value) {
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
    }
    return std::filesystem::path{utf8};
}
} // namespace docenhance::io
