// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/denoise/nlm.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"

#include <cstdint>
#include <expected>
#include <new>
#include <opencv2/core/base.hpp>
#include <opencv2/core/exception.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/core/utils/logger.defines.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <opencv2/photo.hpp>
#include <vector>
namespace docenhance::denoise {
namespace {
// This is OpenCV's fixed error callback ABI. It suppresses foreign stream delivery, not exceptions.
// NOLINTNEXTLINE(google-readability-function-size)
int quiet_error(int /*status*/, const char* /*function*/, const char* /*message*/,
                const char* /*file*/, int /*line*/, void* /*user*/) noexcept {
    return 0;
}
void configure_diagnostics() {
    // Set once before any production native work. No thread policy or per-request global setter.
    static const bool configured = [] {
        cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);
        cv::redirectError(quiet_error);
        return true;
    }();
    static_cast<void>(configured);
}
} // namespace
core::Result<NativeCall> native_tile(image::PlaneView<const std::uint16_t> input,
                                     image::PlaneView<std::uint16_t> output,
                                     const methods::Nlm& method, core::Budget& budget) {
    auto scratch =
        methods::nlm_native_scratch({.width = input.width(), .height = input.height()}, method);
    if (!scratch) {
        return std::unexpected(scratch.error());
    }
    if (input.width() != output.width() || input.height() != output.height() ||
        image::overlaps(input, output)) {
        return core::failure(core::ErrorCode::argument,
                             "Native NLM plane extent or overlap mismatch");
    }
    auto reserved = budget.reserve(*scratch);
    if (!reserved) {
        return std::unexpected(reserved.error());
    }
    const NativeCall observation{
        .reserved_bytes = reserved->size(),
        .charged_bytes = budget.used(),
    };
    try {
        configure_diagnostics();
        // OpenCV's borrowed Mat header requires mutable storage even for an InputArray. The
        // selected overload reads this source; ownership and source identity stay with the caller.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        auto* const borrowed = const_cast<std::uint16_t*>(input.row(0).data());
        const cv::Mat source(static_cast<int>(input.height()), static_cast<int>(input.width()),
                             CV_16UC1, borrowed, input.row_pitch() * sizeof(std::uint16_t));
        cv::Mat destination(static_cast<int>(output.height()), static_cast<int>(output.width()),
                            CV_16UC1, output.row(0).data(),
                            output.row_pitch() * sizeof(std::uint16_t));
        const auto p = method.parameters();
        const auto* const before = destination.data;
        cv::fastNlMeansDenoising(
            source, destination,
            std::vector<float>{static_cast<float>(methods::nlm_native_strength(p))},
            static_cast<int>(p.patch), static_cast<int>(p.search), cv::NORM_L1);
        if (destination.data != before) {
            return core::failure(core::ErrorCode::invariant,
                                 "Native NLM replaced borrowed output storage");
        }
        return observation;
    } catch (const std::bad_alloc&) {
        return core::failure(core::ErrorCode::resource, "Native NLM allocation failed");
    } catch (const cv::Exception& error) {
        if (error.code == cv::Error::StsNoMem) {
            return core::failure(core::ErrorCode::resource, "Native NLM allocation failed");
        }
        return core::failure(core::ErrorCode::numerical, "Native NLM execution failed");
    } catch (...) {
        return core::failure(core::ErrorCode::invariant, "Unexpected native NLM failure");
    }
}
} // namespace docenhance::denoise
