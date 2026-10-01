// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
namespace docenhance::denoise {
// Native-free borrowed views. The caller owns source, destination and tile storage.
struct NativeCall {
    std::size_t reserved_bytes;
    std::size_t charged_bytes;
};
[[nodiscard]] core::Result<NativeCall> native_tile(image::PlaneView<const std::uint16_t> input,
                                                   image::PlaneView<std::uint16_t> output,
                                                   const methods::Nlm& method,
                                                   core::Budget& budget);
struct NlmExecution {
    std::reference_wrapper<const methods::Nlm> method;
    std::reference_wrapper<core::Budget> budget;
    std::reference_wrapper<const core::Cancellation> cancellation;
    std::reference_wrapper<methods::DenoisingReport> report;
};
[[nodiscard]] core::Result<void> denoise(image::PlaneView<const std::uint16_t> input,
                                         image::PlaneView<std::uint16_t> output,
                                         NlmExecution execution);
} // namespace docenhance::denoise
