// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/io/capabilities.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <png.h>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace docenhance::io {
namespace {
constexpr std::uint64_t max_pixels = 40'000'000;
constexpr std::string_view result_name = "result.png";

[[nodiscard]] core::Error png_error(core::ErrorCode code, const png_image& image) {
    return {.code = code, .message = std::string{&image.message[0]}};
}

[[nodiscard]] core::Result<std::filesystem::path>
staging_directory(const std::string& output_directory) {
    const std::filesystem::path target{output_directory};
    const std::filesystem::path parent =
        target.parent_path().empty() ? std::filesystem::path{"."} : target.parent_path();
    std::filesystem::path stage = parent / (target.filename().string() + ".staging");
    std::error_code error;
    if (!std::filesystem::is_directory(parent, error) || error) {
        return core::failure(core::ErrorCode::output, "The output parent directory does not exist");
    }
    if (std::filesystem::exists(target, error) || error) {
        return core::failure(core::ErrorCode::output, "The output directory already exists");
    }
    if (!std::filesystem::create_directory(stage, error)) {
        return core::failure(core::ErrorCode::output,
                             "Cannot create an exclusive staging directory for the output");
    }
    return stage;
}
} // namespace

std::span<const std::string_view> supported_input_formats() noexcept {
    static constexpr auto formats = std::to_array<std::string_view>({"png"});
    return formats;
}

core::Result<image::Plane<std::uint8_t>> load_grayscale_png(const std::string& input,
                                                            core::Budget& budget) {
    png_image decoded{};
    decoded.version = PNG_IMAGE_VERSION;
    if (png_image_begin_read_from_file(&decoded, input.c_str()) == 0) {
        const auto error = png_error(core::ErrorCode::input, decoded);
        return core::failure(error.code, error.message);
    }
    if (decoded.format != PNG_FORMAT_GRAY) {
        png_image_free(&decoded);
        return core::failure(core::ErrorCode::input,
                             "Only 8-bit grayscale PNG input is currently supported");
    }
    const auto pixels = static_cast<std::uint64_t>(decoded.width) * decoded.height;
    if (pixels == 0U || pixels > max_pixels) {
        png_image_free(&decoded);
        return core::failure(core::ErrorCode::resource,
                             "PNG dimensions exceed the 40 megapixel processing limit");
    }
    auto plane = image::Plane<std::uint8_t>::allocate(budget, decoded.width, decoded.height);
    if (!plane) {
        png_image_free(&decoded);
        return core::failure(plane.error().code, plane.error().message);
    }
    decoded.format = PNG_FORMAT_GRAY;
    const auto view = plane->view();
    if (png_image_finish_read(&decoded, nullptr, view.storage().data(),
                              static_cast<int>(view.row_pitch()), nullptr) == 0) {
        const auto error = png_error(core::ErrorCode::input, decoded);
        png_image_free(&decoded);
        return core::failure(error.code, error.message);
    }
    png_image_free(&decoded);
    return plane;
}

core::Result<std::string> publish_grayscale_png(const std::string& output_directory,
                                                image::PlaneView<const std::uint8_t> image) {
    auto stage = staging_directory(output_directory);
    if (!stage) {
        return core::failure(stage.error().code, stage.error().message);
    }
    const auto target = std::filesystem::path{output_directory};
    const auto output = *stage / result_name;
    const auto output_name = output.generic_string();
    png_image encoded{};
    encoded.version = PNG_IMAGE_VERSION;
    encoded.width = image.width();
    encoded.height = image.height();
    encoded.format = PNG_FORMAT_GRAY;
    if (png_image_write_to_file(&encoded, output_name.c_str(), 0, image.storage().data(),
                                static_cast<int>(image.row_pitch()), nullptr) == 0) {
        const auto failure = png_error(core::ErrorCode::output, encoded);
        std::error_code ignored;
        std::filesystem::remove_all(*stage, ignored);
        return core::failure(failure.code, failure.message);
    }
    std::error_code error;
    std::filesystem::rename(*stage, target, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove_all(*stage, ignored);
        return core::failure(core::ErrorCode::output, "Cannot publish the output directory");
    }
    return (target / result_name).string();
}
} // namespace docenhance::io
