// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "diagnostics.hpp"

#include <opencv2/core/utility.hpp>
#include <opencv2/core/utils/logger.defines.hpp>
#include <opencv2/core/utils/logger.hpp>
namespace docenhance::opencv {
namespace {
// This is OpenCV's fixed error callback ABI. It suppresses foreign stream delivery, not exceptions.
// NOLINTNEXTLINE(google-readability-function-size)
int quiet_error(int /*status*/, const char* /*function*/, const char* /*message*/,
                const char* /*file*/, int /*line*/, void* /*user*/) noexcept {
    return 0;
}
} // namespace
void configure_diagnostics() {
    // Set once before any production native work. No thread policy or per-request global setter.
    static const bool configured = [] {
        cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);
        cv::redirectError(quiet_error);
        return true;
    }();
    static_cast<void>(configured);
}
} // namespace docenhance::opencv
