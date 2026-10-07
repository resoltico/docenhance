// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/contrast.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
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
class ContextSource final : public docenhance::image::LinearSource {
  public:
    static constexpr std::uint32_t side = 32;
    explicit ContextSource(docenhance::fuzz::FuzzInput& input) {
        for (auto& value : pixels_) {
            value = static_cast<double>(input.integer<std::uint16_t>()) / 65535;
        }
    }
    [[nodiscard]] docenhance::image::Extent extent() const noexcept override {
        return {.width = side, .height = side};
    }
    docenhance::core::Result<void> read(docenhance::image::RowRange range, std::span<double> rgb,
                                        docenhance::image::RowUse /*use*/) override {
        for (std::size_t i = 0; i < rgb.size() / 3; ++i) {
            std::ranges::fill(rgb.subspan(i * 3, 3),
                              pixels_.at((std::size_t{range.row} * side) + range.first + i));
        }
        return {};
    }

  private:
    std::array<double, std::size_t{side} * side> pixels_{};
};
void contextual(docenhance::fuzz::FuzzInput& input) {
    namespace methods = docenhance::methods;
    namespace image = docenhance::image;
    using docenhance::fuzz::require;
    constexpr std::size_t budget_limit = std::size_t{1024} * 1024;
    docenhance::core::Budget budget{budget_limit};
    const double clip = 1 + (static_cast<double>(input.byte()) / 255 * 7);
    ContextSource source{input};
    const methods::Contrast method =
        methods::Clahe::create({.grid_columns = 2, .grid_rows = 2, .clip = clip}).value();
    methods::ContrastReport report;
    auto model = methods::ContrastModel::prepare(source, {}, method,
                                                 {budget, {}, report, image::RowUse::output});
    require(model.has_value(), "CLAHE bounded preparation");
    std::array<double, std::size_t{ContextSource::side} * image::rgb_channels> rgb{};
    for (std::uint32_t y = 0; y < ContextSource::side; ++y) {
        require(source.read({.row = y}, rgb, image::RowUse::output).has_value(), "CLAHE source");
        require(model->apply({.row = y}, rgb, {}, report, {}).has_value(),
                "CLAHE floating reconstruction");
        require(
            std::ranges::all_of(rgb, [](double f) { return std::isfinite(f) && f >= 0 && f <= 1; }),
            "CLAHE bounded values");
        auto frozen = report;
        auto verification = rgb;
        require(source.read({.row = y}, verification, image::RowUse::verification).has_value(),
                "CLAHE verification input");
        require(model->apply({.row = y}, verification, {}, frozen, {}).has_value(),
                "CLAHE frozen replay");
        require(rgb == verification, "CLAHE immutable maps");
    }
    report.complete = true;
    report.status = report.changed_samples == 0 ? methods::ContrastStatus::no_change
                                                : methods::ContrastStatus::applied;
    report.reason = report.changed_samples == 0 ? methods::ContrastReason::no_effect
                                                : methods::ContrastReason::none;
    require(methods::valid_contrast(report, method), "CLAHE complete observations");
}
void check(std::span<const std::uint8_t> bytes) {
    docenhance::fuzz::FuzzInput input{bytes};
    using docenhance::fuzz::require;
    namespace image = docenhance::image;
    namespace methods = docenhance::methods;
    constexpr unsigned choices = 64;
    const auto count = 1U + (input.byte() % choices);
    const double exponent = .25 + (static_cast<double>(input.byte()) / 255 * 3.75);
    const auto gamma = methods::Gamma::create({.gamma = exponent}).value();
    std::vector<double> values;
    values.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        values.push_back(static_cast<double>(input.integer<std::uint16_t>()) / 65535);
    }
    auto expected = values;
    std::ranges::sort(expected);
    auto sorted = values;
    require(image::sort_samples(sorted).has_value(), "bounded finite percentile sort");
    require(sorted == expected, "independent quantile ordering");
    for (const double p : {0.0, .005, .5, .995, 1.0}) {
        const auto rank = static_cast<std::size_t>(std::ceil(p * count));
        auto index = image::nearest_rank_index(count, p);
        require(index.has_value() && *index == (rank == 0 ? 0 : rank - 1),
                "independent nearest ranks");
    }
    const auto lo = expected.front();
    const auto hi = expected.back();
    for (const auto f : values) {
        auto result = methods::gamma_candidate(f, gamma);
        require(result.has_value() && std::abs(*result - std::pow(f, exponent)) < 1e-12,
                "gamma power reference");
        if (hi - lo >= methods::levels_minimum_range) {
            auto mapped = methods::levels_candidate(f, {.low = lo, .high = hi});
            require(mapped.has_value() &&
                        std::abs(*mapped - std::clamp((f - lo) / (hi - lo), 0.0, 1.0)) < 1e-12,
                    "levels scalar reference");
        }
    }
    docenhance::fuzz::FuzzInput contextual_input{bytes};
    contextual(contextual_input);
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    check({data, size});
    return 0;
}
