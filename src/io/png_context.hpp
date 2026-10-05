// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "file_access.hpp"

#include <array>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <png.h>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace docenhance::io {
struct BundleSlot;
// The callback registry, diagnostics and C handles live OUTSIDE every setjmp frame. No longjmp
// crosses a live non-trivial C++ automatic object. Codec allocations must pass through Budget.
inline constexpr std::size_t codec_allocation_slots = 64;
inline constexpr std::size_t codec_diagnostic_bytes = 160;
inline constexpr int byte_depth = 8;
// The opaque CRT jump buffer requires aligned storage; natural padding is intentional.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
struct PngMemory {
    PngMemory(core::Budget& owner, core::Cancellation control)
        : budget(owner), cancellation(std::move(control)) {}
    std::reference_wrapper<core::Budget> budget;
    std::array<core::Buffer, codec_allocation_slots> blocks{};
    std::array<char, codec_diagnostic_bytes> message{};
    // Native execution state may change through a const borrowed context handle.
    mutable std::jmp_buf jump{};
    bool exhausted = false;
    core::Cancellation cancellation;
    bool cancelled = false;
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
// Reader state and its remaining-byte bound also live outside every libpng jump frame.
struct PngInput {
    void* state = nullptr;
    bool (*read)(void*, std::span<std::uint8_t>) noexcept = nullptr;
    std::size_t remaining = 0;
    core::Checkpoint checkpoint = core::Checkpoint::decode;
};
class PngContext {
  public:
    PngContext(core::Budget& budget, bool write, const core::Cancellation& cancellation = {});
    PngContext(const PngContext&) = delete;
    PngContext& operator=(const PngContext&) = delete;
    PngContext(PngContext&&) = delete;
    PngContext& operator=(PngContext&&) = delete;
    ~PngContext();
    [[nodiscard]] bool create(const BundleSlot& slot);
    [[nodiscard]] bool open_reading(const std::filesystem::path& path);
    [[nodiscard]] bool close_output() noexcept;
    [[nodiscard]] core::Result<void> finish_output(core::Result<void> result);
    [[nodiscard]] core::Error error(core::ErrorCode fallback) const;
    PngMemory memory;
    png_structp png = nullptr;
    png_infop info = nullptr;
    FileHandle file;
    int passes = 1;
    bool writing;
};
void install_png_reader(const PngContext& context, PngInput& input);
[[nodiscard]] bool observe_cancellation(png_structp png, core::Checkpoint at) noexcept;
[[nodiscard]] core::Result<void> encode_png(const BundleSlot& slot,
                                            image::PlaneView<const std::uint8_t> view,
                                            core::Budget& budget,
                                            const core::Cancellation& cancellation = {});
} // namespace docenhance::io
