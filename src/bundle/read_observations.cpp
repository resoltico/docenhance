// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/inventory.hpp"
#include "docenhance/methods/illumination.hpp"
#include "read_fields.hpp"
namespace docenhance::bundle {
bool illumination_agrees(const DeclaredBundle& d) {
    return methods::valid_illumination(
        d.illumination, {.width = d.output.shape.width, .height = d.output.shape.height},
        d.protection.has_value());
}
} // namespace docenhance::bundle
