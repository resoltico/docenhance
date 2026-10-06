// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"

#include <array>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <jpeglib.h>
#include <span>
// These opaque controls belong to this implementation of the public memory-manager API.
struct jvirt_sarray_control {
    JSAMPARRAY rows{};
    JDIMENSION height{};
    JDIMENSION access{};
};
struct jvirt_barray_control {
    JBLOCKARRAY rows{};
    JDIMENSION height{};
    JDIMENSION access{};
};
namespace docenhance::io {
inline constexpr std::size_t jpeg_block_slots = 1024;
inline constexpr std::size_t jpeg_virtual_slots = 8;
inline constexpr std::size_t jpeg_bootstrap_bytes = std::size_t{64} * 1024;
struct JpegMemory {
    jpeg_memory_mgr manager{};
    jpeg_memory_mgr* bootstrap = nullptr;
    std::array<core::Buffer, jpeg_block_slots> blocks;
    std::array<int, jpeg_block_slots> pools{};
    std::array<jvirt_barray_control, jpeg_virtual_slots> virtual_blocks{};
    std::array<jvirt_sarray_control, jpeg_virtual_slots> virtual_samples{};
    std::size_t block_count{};
    std::size_t sample_count{};
};
struct JpegContext {
    JpegContext(core::Budget& working_budget, const core::Cancellation& control,
                std::jmp_buf* native_jump) noexcept
        : budget(working_budget), cancellation(control), jump(native_jump) {}
    JpegContext(const JpegContext&) = delete;
    JpegContext& operator=(const JpegContext&) = delete;
    JpegContext(JpegContext&&) = delete;
    JpegContext& operator=(JpegContext&&) = delete;
    ~JpegContext();
    std::reference_wrapper<core::Budget> budget;
    std::reference_wrapper<const core::Cancellation> cancellation;
    jpeg_decompress_struct decoder{};
    jpeg_error_mgr errors{};
    jpeg_source_mgr source{};
    jpeg_progress_mgr progress{};
    j_decompress_ptr active_decoder = nullptr;
    JpegMemory memory;
    // Null delegates failures to the native decoder's non-returning error handler.
    std::jmp_buf* jump;
    std::span<const std::uint8_t> remaining;
    bool invalid = false;
    bool exhausted = false;
    bool cancelled = false;
    unsigned scan_limit{};
    std::size_t peak_bytes{};
    std::size_t native_block_peak{};
    std::size_t native_allocations{};
    std::size_t working_charge_peak{};
    [[nodiscard]] core::Error error() const;
};
[[nodiscard]] JpegContext& jpeg_context(j_common_ptr decoder) noexcept;
[[noreturn]] void jpeg_failure(j_common_ptr decoder);
void jpeg_checkpoint(j_common_ptr decoder);
void install_jpeg_memory(JpegContext& context, j_common_ptr decoder) noexcept;
void install_jpeg_progress(JpegContext& context, j_decompress_ptr decoder) noexcept;
void install_jpeg_source(JpegContext& context, std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] bool jpeg_header(JpegContext& context, std::span<const std::uint8_t> bytes);
[[nodiscard]] bool jpeg_pixels(JpegContext& context, image::PlaneView<std::uint8_t> pixels);
} // namespace docenhance::io
