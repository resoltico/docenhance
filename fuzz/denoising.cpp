// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/denoising.hpp"

#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/nlm_working.hpp"
#include "docenhance/methods/parameters.hpp"
#include "gamma_fixture.hpp"
#include "nlm_working_fixture.hpp"
#include "nlm_working_reference.hpp"
#include "sharpen_reference.hpp"
#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace {
void compare_output(docenhance::tests::NlmWorkingFixture& fixture,
                    const docenhance::tests::NlmReference& reference,
                    const std::vector<std::uint64_t>& before) {
    const auto output = fixture.luminance.view().storage();
    for (std::size_t i = 0; i < output.size(); ++i) {
        const auto actual = output.subspan(i, 1).front();
        if (std::bit_cast<std::uint64_t>(reference.samples.at(i)) == before.at(i)) {
            docenhance::fuzz::require(std::bit_cast<std::uint64_t>(actual) == before.at(i),
                                      "exact protected/identity/padding bits");
        } else {
            docenhance::fuzz::require(std::isfinite(actual) && actual >= 0 && actual <= 1 &&
                                          std::abs(actual - reference.samples.at(i)) < 1e-12,
                                      "independent exact patch/weight/ordered mean/D3 output");
        }
    }
}
void fill(docenhance::tests::NlmWorkingFixture& fixture, docenhance::fuzz::FuzzInput& input,
          bool absent) {
    for (std::uint32_t y = 0; y < fixture.luminance.height(); ++y) {
        for (std::uint32_t x = 0; x < fixture.luminance.width(); ++x) {
            const auto code = input.integer<std::uint16_t>();
            const auto encoded = code == UINT16_MAX ? 1.0 : (code + 0.25) / 65535;
            fixture.luminance.view().row(y).subspan(x, 1).front() =
                docenhance::tests::sharpen_reference_srgb_decode(encoded);
            const auto protect = input.byte() % 11 == 1;
            fixture.protection.view().row(y).subspan(x, 1).front() = !absent && protect ? 255 : 0;
        }
    }
}
void check(std::span<const std::uint8_t> bytes) {
    namespace methods = docenhance::methods;
    using docenhance::fuzz::require;
    docenhance::fuzz::FuzzInput input{bytes};
    const auto side = 7U + (input.byte() % 8);
    constexpr std::array strengths{0.1, 1.0, 3.0, 25.0};
    constexpr std::array blends{0.5, 1.0, 0.0, 1e-16};
    const methods::NlmParameters p{
        .h = strengths.at(input.byte() % strengths.size()),
        .patch = 3,
        .search = 7,
        .blend = blends.at(input.byte() % blends.size()),
    };
    const bool absent = input.byte() % 2 != 0;
    docenhance::tests::NlmWorkingFixture fixture{side, side, std::size_t{8} * 1024 * 1024};
    fill(fixture, input, absent);
    const auto reference = docenhance::tests::nlm_working_reference(fixture, p);
    const auto before = docenhance::tests::gamma_bits(fixture.luminance.view().storage());
    const auto mask = fixture.protection.view().storage();
    const std::vector<std::uint8_t> mask_before(mask.begin(), mask.end());
    const auto held = fixture.budget.used();
    const docenhance::exec::Scheduler scheduler{
        docenhance::exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto result = methods::apply(methods::Nlm::create(p).value(),
                                       {fixture.luminance.view(),
                                        absent ? docenhance::image::PlaneView<const std::uint8_t>{}
                                               : fixture.protection.view().as_const(),
                                        fixture.budget, scheduler});
    require(result && methods::validate(*result, p) && *result == reference.facts,
            "real Working NLM complete Facts match independent fn!=f write counts, not post-Y "
            "differences");
    compare_output(fixture, reference, before);
    require(fixture.budget.used() == held &&
                std::ranges::equal(mask_before, fixture.protection.view().storage()),
            "real Q/table/fold/band owners refund and all mask backing bytes remain exact");
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    check({data, size});
    return 0;
}
