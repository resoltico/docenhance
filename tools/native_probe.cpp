// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
// Build/test-only. Two jobs, neither of which decodes an untrusted file:
//
// 1. Link-time contracts: every pinned dependency builds, links and computes what it should.
// 2. Memory and threading policy: each imaging library lets this project own its allocation and
//    its parallelism. A document page is large enough that "the library allocates whatever it
//    likes, wherever it likes" is not an acceptable answer, so the hooks that make OpenCV,
//    Leptonica and Little CMS allocate through us are exercised here before any code depends on
//    them. docs/architecture.md records what they are for.
#include <allheaders.h>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <environ.h>
#include <exception>
#include <iostream>
#include <jpeglib.h>
#include <lcms2.h>
#include <new>
#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/core/utils/logger.defines.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <opencv2/photo.hpp>
#include <picosha2.h>
#include <pix_internal.h>
#include <png.h>
#include <string>
#include <tiffio.h>
#include <vector>
#include <zlib.h>
namespace {
// Distinct exit codes identify which dependency contract failed.
constexpr int leptonica_version = 1;
constexpr int jpeg_error_manager = 2;
constexpr int sha256_vector = 3;
constexpr int opencv_denoise = 4;
constexpr int unexpected_exception = 5;
constexpr int opencv_threads = 6;
constexpr int opencv_allocator = 7;
constexpr int leptonica_allocator = 8;
constexpr int lcms_context = 9;
constexpr int jpeg_memory_limit = 10;
constexpr int png_limits = 11;
constexpr int probe_side = 16;
constexpr double probe_value = 32768.0;
constexpr float nlm_strength = 128.0F;
constexpr int nlm_patch = 3;
constexpr int nlm_search = 7;
constexpr std::int64_t jpeg_budget = 64LL * 1024 * 1024;
constexpr std::uint32_t png_limit = 50000;
constexpr double probe_increment = 1.0;
constexpr int probe_depth = 8;

// Leptonica's allocations run through this counter, which a real pipeline would replace with a
// charge against a core::Budget.
std::size_t& leptonica_allocations() {
    static std::size_t count = 0;
    return count;
}
void* leptonica_alloc(std::size_t bytes) {
    ++leptonica_allocations();
    return ::operator new(bytes, std::nothrow);
}
void leptonica_free(void* const data) {
    ::operator delete(data, std::nothrow);
}
void tiff_silent(thandle_t /*handle*/, const char* /*module*/, const char* /*format*/,
                 va_list /*arguments*/) {}

// Third-party diagnostics must never reach this program's streams: OpenCV writes its own log to
// stdout, which is where the JSON response goes, and Leptonica writes to stderr. Both are silenced
// before anything else happens, and the caller checks that stdout stayed clean.
void silence_libraries() {
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);
    setMsgSeverity(L_SEVERITY_NONE);
    TIFFSetErrorHandlerExt(tiff_silent);
    TIFFSetWarningHandlerExt(tiff_silent);
}

// OpenCV picks its own thread count, so a run pins it; and a cv::Mat can be a header over memory
// this project already owns, which is how a page will reach OpenCV: no cv::Mat allocation, no
// hidden copy, and the budget stays the only accounting of the page.
int probe_opencv_policy() {
    cv::setNumThreads(1);
    const auto& info = cv::getBuildInformation();
    const auto at = info.find("Parallel framework");
    const std::string framework = info.substr(at, info.find('\n', at) - at);
    std::cout << framework << " | threads after pinning to one: " << cv::getNumThreads() << '\n';
    // Apple's Grand Central Dispatch owns its own pool and ignores a requested thread count. Every
    // other framework must honour it; either way this program schedules pages and tiles itself
    // rather than delegating the --threads contract to a library.
    if (cv::getNumThreads() != 1 && !framework.contains("GCD")) {
        return opencv_threads;
    }
    std::vector<float> owned(static_cast<std::size_t>(probe_side) * probe_side, 1.0F);
    const cv::Mat source(probe_side, probe_side, CV_32FC1, owned.data());
    cv::Mat destination(probe_side, probe_side, CV_32FC1, owned.data());
    const bool wraps =
        static_cast<const void*>(source.data) == static_cast<const void*>(owned.data()) &&
        source.u == nullptr && destination.u == nullptr;
    const void* const before = destination.data;
    cv::add(source, cv::Scalar(probe_increment), destination);
    const bool computed_in_place = static_cast<const void*>(destination.data) == before &&
                                   owned.front() == 2.0F; // one plus the increment
    return wraps && computed_in_place ? 0 : opencv_allocator;
}

// Leptonica allocates Pix data through a global memory manager, which is ours to install.
int probe_leptonica_policy() {
    setPixMemoryManager(leptonica_alloc, leptonica_free);
    const std::size_t before = leptonica_allocations();
    Pix* pix = pixCreate(probe_side, probe_side, probe_depth);
    const bool counted = pix != nullptr && leptonica_allocations() > before;
    pixDestroy(&pix);
    setPixMemoryManager(nullptr, nullptr);
    return counted ? 0 : leptonica_allocator;
}

// Little CMS isolates state per context, so a run can hold its own transforms and allocator.
int probe_lcms_policy() {
    auto* const context = cmsCreateContext(nullptr, nullptr);
    if (context == nullptr) {
        return lcms_context;
    }
    cmsDeleteContext(context);
    return 0;
}

// The codecs bound their own appetite: libjpeg by a memory ceiling, libpng by image limits.
int probe_codec_policy() {
    jpeg_decompress_struct jpeg{};
    jpeg_error_mgr jpeg_error{};
    jpeg.err = jpeg_std_error(&jpeg_error);
    jpeg_create_decompress(&jpeg);
    jpeg.mem->max_memory_to_use = static_cast<decltype(jpeg.mem->max_memory_to_use)>(jpeg_budget);
    const bool jpeg_bounded = static_cast<std::int64_t>(jpeg.mem->max_memory_to_use) == jpeg_budget;
    jpeg_destroy_decompress(&jpeg);
    if (!jpeg_bounded) {
        return jpeg_memory_limit;
    }
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png == nullptr ? nullptr : png_create_info_struct(png);
    if (png == nullptr || info == nullptr) {
        png_destroy_read_struct(&png, &info, nullptr);
        return png_limits;
    }
    png_set_user_limits(png, png_limit, png_limit);
    const bool png_bounded = static_cast<std::uint32_t>(png_get_user_width_max(png)) == png_limit;
    png_destroy_read_struct(&png, &info, nullptr);
    if (!png_bounded) {
        return png_limits;
    }
    // libtiff reports through a handler, which must be ours rather than stderr.
    const TIFFErrorHandlerExt previous = TIFFSetErrorHandlerExt(tiff_silent);
    TIFFSetErrorHandlerExt(previous);
    return 0;
}

int probe() {
    char* const lept = getLeptonicaVersion();
    if (lept == nullptr) {
        return leptonica_version;
    }
    std::cout << "OpenCV " << cv::getVersionString() << '\n' << lept << '\n';
    lept_free(lept);
    std::cout << "Little CMS API " << cmsGetEncodedCMMversion() << '\n';
    std::cout << "PNG " << png_get_libpng_ver(nullptr) << '\n';
    std::cout << "TIFF " << TIFFGetVersion() << '\n';
    std::cout << "zlib " << zlibVersion() << '\n';
    jpeg_error_mgr jpeg_error{};
    if (jpeg_std_error(&jpeg_error) == nullptr) {
        return jpeg_error_manager;
    }
    const std::string input = "abc";
    const auto hash = picosha2::hash256_hex_string(input);
    if (hash != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
        return sha256_vector;
    }
    const cv::Mat image(probe_side, probe_side, CV_16UC1, cv::Scalar(probe_value));
    cv::Mat result;
    cv::fastNlMeansDenoising(image, result, std::vector<float>{nlm_strength}, nlm_patch, nlm_search,
                             cv::NORM_L1);
    if (result.type() != image.type() || cv::norm(image, result, cv::NORM_INF) != 0.0) {
        return opencv_denoise;
    }
    const auto policies = {
        probe_opencv_policy,
        probe_leptonica_policy,
        probe_lcms_policy,
        probe_codec_policy,
    };
    for (const auto policy : policies) {
        if (const int failed = policy(); failed != 0) {
            return failed;
        }
    }
    std::cout << "Allocation and threading policy: owned\n";
    return 0;
}
} // namespace
int main() { // NOLINT(bugprone-exception-escape): MSVC STL stream failures are caught below.
    try {
        silence_libraries();
        return probe();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    } catch (...) {
        std::cerr << "Unknown non-standard exception\n";
    }
    return unexpected_exception;
}
