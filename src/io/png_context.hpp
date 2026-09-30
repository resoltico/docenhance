// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "entry_identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <png.h>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace docenhance::io {
// The callback registry, diagnostics and C handles live OUTSIDE every setjmp frame. No longjmp
// crosses a live non-trivial C++ automatic object. Codec allocations must pass through Budget.
inline constexpr std::size_t codec_allocation_slots = 64;
inline constexpr std::size_t codec_diagnostic_bytes = 160;
inline constexpr int byte_depth = 8;
struct FileCloser {
    void operator()(std::FILE* file) const noexcept;
};
using FileHandle = std::unique_ptr<std::FILE, FileCloser>;
// Opens a file for reading without building a codec context, for callers that only need bytes.
[[nodiscard]] FileHandle open_for_reading(const std::filesystem::path& path);
// Creates a file, never replacing one: the transaction owns an empty staging directory.
[[nodiscard]] FileHandle open_for_writing(const std::filesystem::path& path);
struct PngMemory {
    PngMemory(core::Budget& owner, core::Cancellation control)
        : budget(owner), cancellation(std::move(control)) {}
    std::reference_wrapper<core::Budget> budget;
    std::array<core::Buffer, codec_allocation_slots> blocks{};
    std::array<char, codec_diagnostic_bytes> message{};
    bool exhausted = false;
    core::Cancellation cancellation;
    bool cancelled = false;
};
// Reader state and its remaining-byte bound also live outside every libpng jump frame.
struct PngInput {
    void* state;
    bool (*read)(void*, std::span<std::uint8_t>) noexcept;
    std::size_t remaining;
};
class PngContext {
  public:
    PngContext(core::Budget& budget, bool write, const core::Cancellation& cancellation = {});
    PngContext(const PngContext&) = delete;
    PngContext& operator=(const PngContext&) = delete;
    PngContext(PngContext&&) = delete;
    PngContext& operator=(PngContext&&) = delete;
    ~PngContext();
    [[nodiscard]] bool open(const std::filesystem::path& path,
                            std::optional<EntryIdentity>* created = nullptr);
    [[nodiscard]] bool close_output() noexcept;
    [[nodiscard]] core::Error error(core::ErrorCode fallback) const;
    PngMemory memory;
    png_structp png = nullptr;
    png_infop info = nullptr;
    FileHandle file;
    int passes = 1;
    bool writing;
};
[[nodiscard]] bool observe_cancellation(png_structp png, core::Checkpoint at) noexcept;
[[nodiscard]] std::filesystem::path utf8_path(std::string_view value);
// The UTF-8 bytes of a path. Where a platform spells paths in bytes those bytes are returned
// unchanged, so a name is recorded exactly as it was admitted; where it spells them in wide
// characters they are converted, never through the active code page, which cannot express the
// characters a document's name is most likely to carry.
[[nodiscard]] std::string utf8_spelling(const std::filesystem::path& value);
[[nodiscard]] core::Result<void> encode_png(const std::filesystem::path& output,
                                            image::PlaneView<const std::uint8_t> view,
                                            core::Budget& budget,
                                            const core::Cancellation& cancellation = {},
                                            std::optional<EntryIdentity>* created = nullptr);
} // namespace docenhance::io
