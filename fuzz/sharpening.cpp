// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/sharpening.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "sharpen_reference.hpp"
#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace {
class Source final : public docenhance::image::LinearSource {
  public:
    static constexpr std::uint32_t width = 9;
    static constexpr std::uint32_t height = 7;
    explicit Source(docenhance::fuzz::FuzzInput& input) {
        for (auto& p : values_) {
            p = static_cast<double>(input.integer<std::uint16_t>()) / 65535;
        }
    }
    [[nodiscard]] docenhance::image::Extent extent() const noexcept override {
        return {.width = width, .height = height};
    }
    docenhance::core::Result<void> read(docenhance::image::RowRange r, std::span<double> rgb,
                                        docenhance::image::RowUse /*use*/) override {
        for (std::size_t i = 0; i < rgb.size() / 3; ++i) {
            std::ranges::fill(rgb.subspan(i * 3, 3),
                              values_.at((std::size_t{r.row} * width) + r.first + i));
        }
        return {};
    }
    [[nodiscard]] double linear(std::size_t index) const {
        return values_.at(index);
    }
    [[nodiscard]] std::vector<double> perceptual() const {
        std::vector<double> result;
        result.reserve(values_.size());
        for (const auto linear : values_) {
            result.push_back(docenhance::tests::sharpen_reference_srgb_encode(linear));
        }
        return result;
    }

  private:
    std::array<double, std::size_t{width} * height> values_{};
};
void scalar_check(docenhance::fuzz::FuzzInput& input, const docenhance::methods::Unsharp& method) {
    const double f = static_cast<double>(input.byte()) / 255;
    const double b = static_cast<double>(input.byte()) / 255;
    const double d = f - b;
    const auto p = method.parameters();
    const double residual =
        std::abs(d) <= p.threshold / 255 ? 0 : d - (d > 0 ? p.threshold / 255 : -p.threshold / 255);
    const auto candidate = docenhance::methods::unsharp_candidate(f, b, method);
    docenhance::fuzz::require(candidate &&
                                  std::abs(*candidate - (f + (p.amount * residual))) < 1e-14,
                              "independent piecewise soft threshold");
}
void reconstruct(const docenhance::methods::SharpenModel& model, Source& source,
                 docenhance::methods::SharpenReport& report,
                 const std::vector<double>& entering, const std::vector<double>& expected) {
    namespace image = docenhance::image;
    using docenhance::fuzz::require;
    std::array<double, Source::width * image::rgb_channels> rgb{};
    for (std::uint32_t y = 0; y < Source::height; ++y) {
        require(source.read({.row = y}, rgb, image::RowUse::output).has_value(),
                "sharpening source");
        auto verification = rgb;
        auto ignored = report;
        require(model.apply({.row = y}, rgb, {}, report, {}).has_value(), "sharpening application");
        require(model.apply({.row = y}, verification, {}, ignored, {}).has_value(),
                "immutable sharpening replay");
        require(rgb == verification, "replay samples identical");
        for (std::size_t x = 0; x < Source::width; ++x) {
            const auto index = (std::size_t{y} * Source::width) + x;
            const auto target = docenhance::tests::sharpen_reference_linear_output(
                source.linear(index), entering.at(index), expected.at(index));
            require(docenhance::tests::sharpen_reference_gray_rgb_matches(
                        std::span<const double>{rgb}.subspan(x * image::rgb_channels,
                                                               image::rgb_channels),
                        target),
                    "independent direct 2D Gaussian and linear RGB output");
        }
        require(
            std::ranges::all_of(rgb, [](double p) { return std::isfinite(p) && p >= 0 && p <= 1; }),
            "finite unit transported output");
    }
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace methods = docenhance::methods;
    namespace image = docenhance::image;
    using docenhance::fuzz::require;
    docenhance::fuzz::FuzzInput input{{data, size}};
    constexpr std::size_t limit = std::size_t{1024} * 1024;
    docenhance::core::Budget budget{limit};
    const double amount = static_cast<double>(input.byte()) / 255 * 2;
    const double threshold = static_cast<double>(input.byte()) / 255 * 20;
    const double sigma = 0.3 + (static_cast<double>(input.byte()) / 255 * 2.7);
    const auto method =
        methods::Unsharp::create({.sigma = sigma, .amount = amount, .threshold = threshold});
    require(method.has_value(), "bounded unsharp admission");
    scalar_check(input, *method);
    Source source{input};
    methods::SharpenReport report;
    auto model = methods::SharpenModel::prepare(source, {}, *method,
                                                {budget, {}, report, image::RowUse::output});
    require(model.has_value(), "bounded immutable sharpening preparation");
    if (!model->active()) {
        return 0;
    }
    const auto entering = source.perceptual();
    const auto expected =
        docenhance::tests::sharpen_reference(entering, {
                                                                      .width = Source::width,
                                                                      .height = Source::height,
                                                                      .sigma = sigma,
                                                                      .amount = amount,
                                                                      .threshold = threshold,
                                                                  });
    reconstruct(*model, source, report, entering, expected);
    report.complete = true;
    report.status = report.changed_samples == 0 ? methods::SharpenStatus::no_change
                                                : methods::SharpenStatus::applied;
    report.reason = report.changed_samples == 0 ? methods::SharpenReason::no_effect
                                                : methods::SharpenReason::none;
    require(methods::valid_sharpen(report, methods::Sharpening{*method}),
            "complete sharpening observations");
    return 0;
}
