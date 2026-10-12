// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"
namespace docenhance::methods {
// One run owns these observations; records and responses consume that same fixed sequence.
struct StageObservations {
    IlluminationReport illumination{};
    DenoisingReport denoising{};
    RestorationReport restoration{};
    ContrastReport contrast{};
    SharpenReport sharpening{};
};
} // namespace docenhance::methods
