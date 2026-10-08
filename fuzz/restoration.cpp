// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/restoration.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/opencv/restoration.hpp"
#include "support/entry_point.hpp"
#include "support/fft_dispatch.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
#include <utility>
namespace {
namespace image = docenhance::image;
namespace methods = docenhance::methods;
class Source final : public image::LinearSource {
  public:
    [[nodiscard]] image::Extent extent() const noexcept override {
        return {.width = 3, .height = 3};
    }
    docenhance::core::Result<void> read(image::RowRange range, std::span<double> rgb,
                                        image::RowUse /*use*/) override {
        for (std::size_t i = 0; i < rgb.size() / image::rgb_channels; ++i) {
            const auto x = range.first + i;
            const auto y = 0.4 + (0.1 * std::cos((std::numbers::pi * static_cast<double>(x)) / 2));
            for (std::size_t c = 0; c < image::rgb_channels; ++c) {
                rgb[(i * image::rgb_channels) + c] = y;
            }
        }
        return {};
    }
};
void phase_case(double k, double blend, double mass) {
    using docenhance::fuzz::require;
    docenhance::core::Budget budget{std::size_t{16} * 1024 * 1024};
    const auto method = methods::Wiener::create(
        {.psf = methods::FilePsf{.path = "phase.png"}, .k = k, .blend = blend});
    require(method.has_value(), "bounded restoration method");
    methods::ResolvedPsf psf{
        .width = 3,
        .height = 3,
        .coefficients = {0, 0, 0, 0, 1 - mass, mass, 0, 0, 0},
        .centroid_x = mass,
        .centroid_y = 0,
        .source_identity =
            methods::PsfIdentity{
                .path = "phase.png",
                .identity = {.sha256 = std::string(64, '0'), .bytes = 1},
            },
    };
    Source source;
    methods::RestorationReport report;
    {
        auto model = docenhance::opencv::RestorationModel::prepare(
            source, {}, *method, std::move(psf), {budget, {}, report, image::RowUse::output});
        require(model.has_value() && model->active(), "native phase preparation");
        require(report.fft_width == 72 && report.fft_height == 72,
                "reflected periodic analytic fixture");
        const std::complex<double> h{1 - mass, -mass};
        const auto filter = std::conj(h) / (std::norm(h) + k);
        std::array<double, 9> row{};
        for (std::uint32_t y = 0; y < 3; ++y) {
            require(source.read({.row = y}, row, image::RowUse::output).has_value(),
                    "native source row");
            const auto original = row;
            auto replay = row;
            methods::RestorationReport ignored;
            require(model->apply({.row = y}, row, {}, report, {}).has_value(),
                    "native phase apply");
            require(model->apply({.row = y}, replay, {}, ignored, {}).has_value(),
                    "immutable native replay");
            require(row == replay, "verification identical without another transform");
            for (std::size_t x = 0; x < 3; ++x) {
                const auto wave = std::polar(1.0, std::numbers::pi * static_cast<double>(x) / 2);
                const auto candidate = 0.4 + (0.1 * std::real(filter * wave));
                const auto expected = ((1 - blend) * original.at(x * 3)) + (blend * candidate);
                require(std::abs(row.at(x * 3) - expected) < 2e-6,
                        "independent asymmetric Fourier phase/DC");
            }
        }
        report.complete = true;
        report.status = report.changed_samples == 0 ? methods::RestorationStatus::no_change
                                                    : methods::RestorationStatus::applied;
        report.reason = report.changed_samples == 0 ? methods::RestorationReason::no_effect
                                                    : methods::RestorationReason::none;
        require(methods::valid_restoration(report, methods::Restoration{*method}),
                "native complete observations");
        require(report.native_calls == 3, "verification does not transform again");
    }
    require(budget.used() == 0, "native restoration payload refunded");
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    docenhance::fuzz::FuzzInput input{{data, size}};
    const auto k = 1e-5 + ((static_cast<double>(input.byte()) / 255) * (1 - 1e-5));
    const auto blend = 0.01 + ((static_cast<double>(input.byte()) / 255) * 0.99);
    const auto mass = static_cast<double>(input.byte()) / 255;
    docenhance::fuzz::check_fft_dispatch(static_cast<std::size_t>(mass * 255) %
                                         docenhance::fuzz::dispatch_samples);
    phase_case(k, blend, mass);
    return 0;
}
