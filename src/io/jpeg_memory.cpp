// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/memory.hpp"
#include "jpeg_context.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
namespace docenhance::io {
namespace {
// Allocation and exception handling finish before a native error jump can occur.
void* allocate(JpegContext& context, int pool, std::size_t size) noexcept {
    try {
        if (pool < JPOOL_PERMANENT || pool >= JPOOL_NUMPOOLS) {
            return nullptr;
        }
        for (std::size_t i = 0; i < context.memory.blocks.size(); ++i) {
            auto& block = context.memory.blocks.at(i);
            if (!block.empty()) {
                continue;
            }
            auto bytes = context.budget.get().allocate(size);
            if (!bytes) {
                break;
            }
            block = std::move(*bytes);
            context.memory.pools.at(i) = pool;
            ++context.native_allocations;
            std::size_t charged = 0;
            for (const auto& owned : context.memory.blocks) {
                charged += owned.size();
            }
            context.native_block_peak = std::max(context.native_block_peak, charged);
            context.peak_bytes = std::max(context.peak_bytes, charged + jpeg_bootstrap_bytes);
            context.working_charge_peak =
                std::max(context.working_charge_peak, context.budget.get().used());
            return block.bytes().data();
        }
    } catch (...) {
        context.exhausted = true;
    }
    context.exhausted = true;
    return nullptr;
}
void* native_allocate(j_common_ptr decoder, int pool, std::size_t size) {
    jpeg_checkpoint(decoder);
    auto* const result = allocate(jpeg_context(decoder), pool, size);
    if (result == nullptr) {
        jpeg_failure(decoder);
    }
    return result;
}
std::size_t product(j_common_ptr decoder, std::size_t a, std::size_t b) {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
        jpeg_context(decoder).exhausted = true;
        jpeg_failure(decoder);
    }
    return a * b;
}
// Native SIMD routines need aligned rows and safe padding at the right edge.
std::size_t stride(j_common_ptr decoder, std::size_t width, std::size_t element) {
    const auto bytes = product(decoder, width, element);
    constexpr auto alignment = core::buffer_alignment;
    if (bytes > std::numeric_limits<std::size_t>::max() - alignment) {
        jpeg_failure(decoder);
    }
    return ((bytes + alignment - 1) / alignment) * alignment;
}
void zero(j_common_ptr decoder, std::span<std::byte> bytes) {
    constexpr std::size_t transfer = std::size_t{64} * 1024;
    while (!bytes.empty()) {
        jpeg_checkpoint(decoder);
        const auto part = bytes.first(std::min(transfer, bytes.size()));
        std::ranges::fill(part, std::byte{});
        bytes = bytes.subspan(part.size());
    }
}
JSAMPARRAY samples(j_common_ptr decoder, int pool, JDIMENSION width, JDIMENSION height) {
    const auto pitch = stride(decoder, width, sizeof(JSAMPLE));
    auto* const pointers = static_cast<JSAMPROW*>(
        native_allocate(decoder, pool, product(decoder, height, sizeof(JSAMPROW))));
    auto* const data =
        static_cast<JSAMPLE*>(native_allocate(decoder, pool, product(decoder, height, pitch)));
    for (JDIMENSION row = 0; row < height; ++row) {
        jpeg_checkpoint(decoder);
        std::span{pointers, height}.subspan(row, 1).front() =
            std::span{data, product(decoder, height, pitch)}
                .subspan(std::size_t{row} * pitch)
                .data();
    }
    return pointers;
}
JBLOCKARRAY blocks(j_common_ptr decoder, int pool, JDIMENSION width, JDIMENSION height) {
    const auto pitch = stride(decoder, width, sizeof(JBLOCK));
    auto* const pointers = static_cast<JBLOCKROW*>(
        native_allocate(decoder, pool, product(decoder, height, sizeof(JBLOCKROW))));
    auto* const data =
        static_cast<JBLOCK*>(native_allocate(decoder, pool, product(decoder, height, pitch)));
    for (JDIMENSION row = 0; row < height; ++row) {
        jpeg_checkpoint(decoder);
        std::span{pointers, height}.subspan(row, 1).front() =
            std::span{data, product(decoder, height, pitch / sizeof(JBLOCK))}
                .subspan(std::size_t{row} * (pitch / sizeof(JBLOCK)))
                .data();
    }
    return pointers;
}
// Fixed six-argument callback signature in libjpeg's public memory-manager ABI.
// NOLINTNEXTLINE(readability-function-size)
jvirt_barray_ptr virtual_blocks(j_common_ptr decoder, int pool, boolean /*pre_zero*/,
                                JDIMENSION width, JDIMENSION height, JDIMENSION access) {
    auto& context = jpeg_context(decoder);
    if (pool != JPOOL_IMAGE || context.memory.block_count == jpeg_virtual_slots) {
        jpeg_failure(decoder);
    }
    auto& control =
        std::span{context.memory.virtual_blocks}.subspan(context.memory.block_count++, 1).front();
    control = {.rows = blocks(decoder, pool, width, height), .height = height, .access = access};
    const auto pitch = stride(decoder, width, sizeof(JBLOCK));
    // Progressive coefficient state is zero initialized, never backed by a temporary file.
    for (JDIMENSION row = 0; row < height; ++row) {
        zero(decoder,
             std::as_writable_bytes(std::span{
                 std::span{control.rows, height}.subspan(row, 1).front(), pitch / sizeof(JBLOCK)}));
    }
    return &control;
}
// Fixed six-argument callback signature in libjpeg's public memory-manager ABI.
// NOLINTNEXTLINE(readability-function-size)
jvirt_sarray_ptr virtual_samples(j_common_ptr decoder, int pool, boolean /*pre_zero*/,
                                 JDIMENSION width, JDIMENSION height, JDIMENSION access) {
    auto& context = jpeg_context(decoder);
    if (pool != JPOOL_IMAGE || context.memory.sample_count == jpeg_virtual_slots) {
        jpeg_failure(decoder);
    }
    auto& control =
        std::span{context.memory.virtual_samples}.subspan(context.memory.sample_count++, 1).front();
    control = {.rows = samples(decoder, pool, width, height), .height = height, .access = access};
    const auto pitch = stride(decoder, width, sizeof(JSAMPLE));
    for (JDIMENSION row = 0; row < height; ++row) {
        zero(decoder, std::as_writable_bytes(std::span{
                          std::span{control.rows, height}.subspan(row, 1).front(), pitch}));
    }
    return &control;
}
void realize(j_common_ptr decoder) {
    // Virtual arrays are fully charged and allocated at request time, before entropy work.
    jpeg_checkpoint(decoder);
}
JBLOCKARRAY access_blocks(j_common_ptr decoder, jvirt_barray_ptr control, JDIMENSION start,
                          JDIMENSION count, boolean /*writable*/) {
    if (control == nullptr || count > control->access || start > control->height ||
        count > control->height - start) {
        jpeg_failure(decoder);
    }
    jpeg_checkpoint(decoder);
    return std::span{control->rows, control->height}.subspan(start).data();
}
JSAMPARRAY access_samples(j_common_ptr decoder, jvirt_sarray_ptr control, JDIMENSION start,
                          JDIMENSION count, boolean /*writable*/) {
    if (control == nullptr || count > control->access || start > control->height ||
        count > control->height - start) {
        jpeg_failure(decoder);
    }
    jpeg_checkpoint(decoder);
    return std::span{control->rows, control->height}.subspan(start).data();
}
void free_pool(j_common_ptr decoder, int pool) noexcept {
    auto& memory = jpeg_context(decoder).memory;
    for (std::size_t i = 0; i < memory.blocks.size(); ++i) {
        if (std::span{memory.pools}.subspan(i, 1).front() == pool) {
            std::span{memory.blocks}.subspan(i, 1).front() = {};
        }
    }
    if (pool == JPOOL_IMAGE) {
        memory.block_count = 0;
        memory.sample_count = 0;
    }
}
void destroy(j_common_ptr decoder) noexcept {
    auto& memory = jpeg_context(decoder).memory;
    free_pool(decoder, JPOOL_IMAGE);
    free_pool(decoder, JPOOL_PERMANENT);
    decoder->mem = memory.bootstrap;
    memory.bootstrap->self_destruct(decoder);
    memory.bootstrap = nullptr;
}
} // namespace
void install_jpeg_memory(JpegContext& context) noexcept {
    context.memory.bootstrap = context.decoder.mem;
    context.memory.manager = {
        .alloc_small = native_allocate,
        .alloc_large = native_allocate,
        .alloc_sarray = samples,
        .alloc_barray = blocks,
        .request_virt_sarray = virtual_samples,
        .request_virt_barray = virtual_blocks,
        .realize_virt_arrays = realize,
        .access_virt_sarray = access_samples,
        .access_virt_barray = access_blocks,
        .free_pool = free_pool,
        .self_destruct = destroy,
        .max_memory_to_use = 0,
        .max_alloc_chunk = std::numeric_limits<decltype(jpeg_memory_mgr::max_alloc_chunk)>::max(),
    };
    context.decoder.mem = &context.memory.manager;
}
} // namespace docenhance::io
