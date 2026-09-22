// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <png.h>
#include <string>
#include <string_view>

namespace docenhance::io {
// The callback registry, diagnostics and C handles live OUTSIDE every setjmp frame. No longjmp
// crosses a live non-trivial C++ automatic object. Codec allocations must pass through Budget.
inline constexpr std::size_t codec_allocation_slots = 64;
inline constexpr std::size_t codec_diagnostic_bytes = 160;
inline constexpr int byte_depth = 8;
struct FileCloser {
    void operator()(std::FILE* file) const noexcept;
};
struct PngMemory {
    std::reference_wrapper<core::Budget> budget;
    std::array<core::Buffer, codec_allocation_slots> blocks;
    std::array<char, codec_diagnostic_bytes> message{};
    bool exhausted = false;
};
class PngContext {
  public:
    PngContext(core::Budget& budget, bool write);
    PngContext(const PngContext&) = delete;
    PngContext& operator=(const PngContext&) = delete;
    PngContext(PngContext&&) = delete;
    PngContext& operator=(PngContext&&) = delete;
    ~PngContext();
    [[nodiscard]] bool open(const std::filesystem::path& path);
    [[nodiscard]] bool close_output() noexcept;
    [[nodiscard]] core::Error error(core::ErrorCode fallback) const;
    PngMemory memory;
    png_structp png = nullptr;
    png_infop info = nullptr;
    std::unique_ptr<std::FILE, FileCloser> file;
    int passes = 1;
    bool writing;
};
[[nodiscard]] std::filesystem::path utf8_path(std::string_view value);
[[nodiscard]] core::Result<void> encode_png(const std::filesystem::path& output,
                                            image::PlaneView<const std::uint8_t> view,
                                            core::Budget& budget);
} // namespace docenhance::io
