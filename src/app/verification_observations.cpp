// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/verify.hpp"
#include "docenhance/contract/cli_contract.hpp"
#include "docenhance/core/bundle_path.hpp"
#include "docenhance/core/identity.hpp"
#include "observations.hpp"

#include <cstddef>
namespace docenhance::app {
bool valid_verified(const Verified& value, const VerifyRequest& request) {
    if (value.directory != request.directory() ||
        !core::valid_hexadecimal(value.run, core::run_identity_hex_length) ||
        !core::valid_instant(value.recorded) || value.confirmed.empty() ||
        value.confirmed.size() > contract::response_confirmed_limit) {
        return false;
    }
    for (std::size_t i = 0; i < value.confirmed.size(); ++i) {
        const auto& entry = value.confirmed.at(i);
        if (!core::valid_bundle_path(entry.name) ||
            (entry.identity.bytes == 0 ||
             !core::valid_hexadecimal(entry.identity.sha256, core::sha256_hex_length))) {
            return false;
        }
        for (std::size_t prior = 0; prior < i; ++prior) {
            if (value.confirmed.at(prior).name == entry.name) {
                return false;
            }
        }
    }
    return true;
}
} // namespace docenhance::app
