// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
// The engine-agnostic entry point every harness defines. libFuzzer, AFL++ and the replay driver
// all call it; declaring it once keeps -Wmissing-declarations strict for the definitions.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);
