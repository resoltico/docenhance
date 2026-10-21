// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/fft/fft.hpp"
#include "docenhance/image/plane.hpp"
#include "fft_detail.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <pocketfft_hdronly.h>
namespace docenhance::fft {
unsigned native_lanes() noexcept {
#ifdef POCKETFFT_NO_VECTORS
    return 1;
#elifdef __AVX512F__
    constexpr unsigned lanes = 8;
    return lanes;
#elif defined(__AVX__) || defined(__riscv_vector) || defined(__riscv_v_intrinsic)
    constexpr unsigned lanes = 4;
    return lanes;
#else
    return 2;
#endif
}
namespace {
core::Result<void> forward_rows(const Transform& t, std::uint32_t first, std::uint32_t count) {
    try {
        const auto shape = t.spectrum.get().shape();
        const auto real_stride = static_cast<std::ptrdiff_t>(t.input.shape().stride);
        const auto complex_stride = static_cast<std::ptrdiff_t>(shape.stride);
        pocketfft::r2c<double>({count, t.input.width()}, {real_stride, sizeof(double)},
                               {complex_stride, sizeof(Complex)}, std::size_t{1},
                               pocketfft::FORWARD, t.input.row(first).data(),
                               t.spectrum.get().row(first).data(), 1.0, 1);
    } catch (const std::bad_alloc&) {
        return failed(core::ErrorCode::resource);
    } catch (...) {
        return failed(core::ErrorCode::invariant);
    }
    return {};
}
core::Result<void> inverse_rows(const Transform& t, std::uint32_t first, std::uint32_t count) {
    try {
        const auto extent = t.spectrum.get().extent();
        const auto real_stride = static_cast<std::ptrdiff_t>(t.output.shape().stride);
        const auto complex_stride = static_cast<std::ptrdiff_t>(t.spectrum.get().shape().stride);
        const auto samples = std::uint64_t{extent.width} * extent.height;
        pocketfft::c2r<double>({count, extent.width}, {complex_stride, sizeof(Complex)},
                               {real_stride, sizeof(double)}, std::size_t{1}, pocketfft::BACKWARD,
                               t.spectrum.get().row(first).data(), t.output.row(first).data(),
                               1.0 / static_cast<double>(samples), 1);
    } catch (const std::bad_alloc&) {
        return failed(core::ErrorCode::resource);
    } catch (...) {
        return failed(core::ErrorCode::invariant);
    }
    return {};
}
} // namespace
core::Result<void> native_rows(const Transform& t, std::uint32_t first, std::uint32_t count) {
    return t.inverse ? inverse_rows(t, first, count) : forward_rows(t, first, count);
}
core::Result<void> native_columns(const Transform& t, std::uint32_t first, std::uint32_t count) {
    try {
        const auto stride = static_cast<std::ptrdiff_t>(t.spectrum.get().shape().stride);
        auto* const column = t.spectrum.get().row(0).subspan(first, 1).data();
        const pocketfft::stride_t strides{stride, sizeof(Complex)};
        pocketfft::c2c<double>({t.spectrum.get().extent().height, count}, strides, strides, {0},
                               !t.inverse, column, column, 1.0, 1);
    } catch (const std::bad_alloc&) {
        return failed(core::ErrorCode::resource);
    } catch (...) {
        return failed(core::ErrorCode::invariant);
    }
    return {};
}
} // namespace docenhance::fft
