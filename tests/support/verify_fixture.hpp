// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/host/processor.hpp"
#include "docenhance/host/verifier.hpp"
#include "docenhance/record/record.hpp"
#include "file_contents.hpp"
#include "png_fixture.hpp"
#include "temporary_directory.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <span>
#include <string>
#include <utility>
#include <vector>
namespace docenhance::tests {
struct VerifyFixtureSettings {
    bool assets = false;
    bool binary = true;
    std::uint32_t width = 8;
};
inline void write_fixture_png(const std::filesystem::path& path,
                              const std::vector<std::uint8_t>& encoded) {
    std::ofstream writing{path, std::ios::binary};
    for (const auto byte : encoded) {
        writing.put(static_cast<char>(byte));
    }
    writing.flush();
    REQUIRE(writing.good());
}
void write_verified_source(const std::filesystem::path& input,
                           const std::vector<std::uint8_t>& encoded);
inline record::RunContext pinned_context() {
    return {.identity = std::string(32, 'a'), .recorded = "2026-01-01T00:00:00Z"};
}
// A published bundle to read back, produced by the real processing path.
inline std::filesystem::path published_bundle(const TemporaryDirectory& temporary,
                                              const std::string& name, host::Processor& processor,
                                              VerifyFixtureSettings settings = {}) {
    const auto input = temporary.path / (name + "-page.png");
    const GrayFixture fixture{
        .width = settings.width,
        .height = 4,
        .depth = 8,
        .interlaced = false,
        .samples = std::vector<std::uint8_t>(std::size_t{settings.width} * 4, 128),
    };
    const auto encoded = make_gray_png(fixture);
    write_verified_source(input, encoded);
    app::Invocation invocation;
    invocation.command = app::Command::process;
    invocation.operands = {utf8_spelling(input)};
    invocation.option(app::OptionId::out_dir) = utf8_spelling(temporary.path / name);
    invocation.option(app::OptionId::output_mode) = settings.binary ? "bw" : "preserve";
    if (settings.binary) {
        invocation.option(app::OptionId::binarize) = "fixed";
        invocation.option(app::OptionId::fixed_threshold) = "0.5";
    }
    if (settings.assets) {
        const auto mask = temporary.path / (name + "-mask.png");
        const auto psf = temporary.path / (name + "-psf.png");
        auto protected_pixels = std::vector<std::uint8_t>(std::size_t{settings.width} * 4, 0);
        protected_pixels.front() = 1;
        write_fixture_png(mask, make_gray_png({
                                    .width = settings.width,
                                    .height = 4,
                                    .depth = 8,
                                    .interlaced = false,
                                    .samples = std::move(protected_pixels),
                                }));
        write_fixture_png(psf, make_gray_png({
                                   .width = 3,
                                   .height = 3,
                                   .depth = 8,
                                   .interlaced = false,
                                   .samples = {0, 0, 0, 0, 255, 0, 0, 0, 0},
                               }));
        invocation.option(app::OptionId::protect_mask) = utf8_spelling(mask);
        invocation.option(app::OptionId::deblur) = "wiener";
        invocation.option(app::OptionId::psf) = "kernel";
        invocation.option(app::OptionId::psf_file) = utf8_spelling(psf);
        invocation.option(app::OptionId::deblur_blend) = "0";
    }
    auto request = app::prepare_process(invocation);
    REQUIRE(request);
    const auto published = processor.process(*request, {});
    REQUIRE(published);
    return temporary.path / name;
}

inline core::Result<app::Verified> read_back(const std::filesystem::path& directory) {
    app::Invocation invocation;
    invocation.command = app::Command::verify;
    invocation.operands = {utf8_spelling(directory)};
    auto request = app::prepare_verify(invocation);
    REQUIRE(request);
    host::Verifier verifier;
    return verifier.verify(*request, {});
}
} // namespace docenhance::tests

namespace docenhance::tests {
inline std::vector<core::NamedContent>
verified_expected_files(const std::filesystem::path& bundle,
                        std::initializer_list<const char*> names) {
    std::vector<core::NamedContent> result;
    for (const auto* const name : names) {
        const auto bytes = file_contents(bundle / name);
        const auto identity = fs::identify(std::as_bytes(std::span{bytes}));
        REQUIRE(identity);
        result.push_back({.name = name, .identity = *identity});
    }
    return result;
}
inline void check_verified_files(const app::Verified& observed,
                                 const std::vector<core::NamedContent>& expected) {
    REQUIRE(observed.confirmed.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(observed.confirmed.at(i).name == expected.at(i).name);
        CHECK(observed.confirmed.at(i).identity == expected.at(i).identity);
    }
    CHECK(std::ranges::count(observed.confirmed, record::record_name, &core::NamedContent::name) ==
          1);
}
} // namespace docenhance::tests
