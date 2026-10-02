// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "png_context.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "entry_identity.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <optional>
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
    png_longjmp(png, 1);
}
void warn_png(png_structp png, png_const_charp message) noexcept {
    // Corrupt/truncated metadata is not silently accepted as a successfully decoded document.
    fail_png(png, message);
}
// A template so that only the branch for this platform's path type is ever instantiated: in a
// plain function the other branch would still have to type-check, and returning a wide string as
// a narrow one does not.
template <typename Path> std::string spelled_in_utf8(const Path& value) {
    if constexpr (std::same_as<typename Path::value_type, char>) {
        return value.native();
    } else {
        std::string bytes;
        for (const char8_t unit : value.u8string()) {
            bytes.push_back(static_cast<char>(unit));
        }
        return bytes;
    }
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
FileHandle open_for_reading(const std::filesystem::path& path) {
#ifdef _WIN32
    std::FILE* opened = nullptr;
    // NOLINTNEXTLINE(misc-include-cleaner): MSVC exposes _wfopen_s through C runtime internals.
    if (_wfopen_s(&opened, path.c_str(), L"rb") != 0) {
        return {};
    }
    return FileHandle{opened};
#else
    // The handle immediately takes ownership of the C stream.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return FileHandle{std::fopen(path.c_str(), "rb")};
#endif
}
FileHandle open_for_writing(const std::filesystem::path& path) {
#ifdef _WIN32
    std::FILE* opened = nullptr;
    // NOLINTNEXTLINE(misc-include-cleaner): MSVC exposes _wfopen_s through C runtime internals.
    if (_wfopen_s(&opened, path.c_str(), L"wbx") != 0) {
        return {};
    }
    return FileHandle{opened};
#else
    // The handle immediately takes ownership of the C stream.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return FileHandle{std::fopen(path.c_str(), "wbx")};
#endif
}
bool PngContext::open(const std::filesystem::path& path,
                      std::optional<EntryIdentity>* const created) {
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
    if (file != nullptr && created != nullptr) {
        *created = stream_identity(file.get());
    }
    return file != nullptr && (created == nullptr || created->has_value());
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
std::string utf8_spelling(const std::filesystem::path& value) {
    return spelled_in_utf8(value);
}
std::filesystem::path utf8_path(std::string_view value) {
    // The admitted spelling is already UTF-8 bytes. Where a path stores char those bytes are its
    // native representation, so they are kept verbatim; only a wchar_t path (Windows) needs the
    // char8_t overload, which decodes them instead of applying the active code page. Neither
    // branch normalizes or repairs the identity bytes.
    if constexpr (std::same_as<std::filesystem::path::value_type, char>) {
        return std::filesystem::path{std::string{value}};
    } else {
        std::u8string utf8;
        utf8.reserve(value.size());
        for (const char byte : value) {
            utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
        }
        return std::filesystem::path{utf8};
    }
}
} // namespace docenhance::io
