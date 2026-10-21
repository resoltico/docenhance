// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/restoration.hpp"
#include "restoration_detail.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <variant>
namespace docenhance::methods {
bool canonical_psf(const WienerParameters& p, const ResolvedPsf& psf) {
    if (p.psf.valueless_by_exception() || !validate_psf(psf)) {
        return false;
    }
    if (std::holds_alternative<Kernel>(p.psf)) {
        return psf.source_identity && psf.source_identity->bytes > 0 &&
               psf.source_identity->bytes <= core::psf_encoded_bytes_max &&
               core::valid_hexadecimal(psf.source_identity->sha256, core::sha256_hex_length);
    }
    const auto side = psf_side(p.psf);
    if (!side || *side != psf.width || *side != psf.height || psf.source_identity) {
        return false;
    }
    std::array<double, restoration_analytic_coefficients_max> storage{};
    auto const expected = std::span<double>{storage}.first(std::size_t{*side} * (*side));
    const auto generated = std::holds_alternative<GaussianPsf>(p.psf)
                               ? gaussian_psf(std::get<GaussianPsf>(p.psf), expected)
                               : motion_psf(std::get<MotionPsf>(p.psf), expected);
    if (!generated) {
        return false;
    }
    constexpr double coefficient_tolerance = 1e-14;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (std::abs(expected.subspan(i, 1).front() -
                     std::span<const double>{psf.coefficients}.subspan(i, 1).front()) >
            coefficient_tolerance) {
            return false;
        }
    }
    return true;
}
} // namespace docenhance::methods
