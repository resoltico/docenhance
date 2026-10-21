// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/host/processor.hpp"
#include "file_contents.hpp"
#include "temporary_directory.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <ios>
namespace docenhance::tests {
namespace {
app::Invocation selected(const std::filesystem::path& source, const std::filesystem::path& output) {
    app::Invocation value;
    value.command = app::Command::process;
    value.operands = {utf8_spelling(source)};
    value.option(app::OptionId::out_dir) = utf8_spelling(output);
    value.option(app::OptionId::illumination) = "surface";
    value.option(app::OptionId::denoise) = "nlm";
    value.option(app::OptionId::deblur) = "wiener";
    value.option(app::OptionId::psf) = "gaussian";
    value.option(app::OptionId::contrast) = "clahe";
    value.option(app::OptionId::sharpen) = "unsharp";
    return value;
}
} // namespace
TEST_CASE("Unstarted selected stages stay absent after real acquisition or codec failure",
          "[host]") {
    const TemporaryDirectory directory{"docenhance-unstarted"};
    for (const bool acquired : {false, true}) {
        const auto source = directory.path / (acquired ? "malformed.png" : "absent.png");
        const auto output = directory.path / (acquired ? "decode-output" : "acquisition-output");
        if (acquired) {
            std::ofstream file{source, std::ios::binary};
            file << "not an image";
            file.close();
            REQUIRE(file);
        }
        const auto request = app::prepare_process(selected(source, output));
        REQUIRE(request);
        host::Processor processor;
        const auto result = processor.process(*request, {});
        REQUIRE(!result);
        const auto& failure = result.error();
        CHECK(failure.error.code == core::ErrorCode::input);
        CHECK(failure.publication == core::Publication::not_started);
        CHECK(!failure.failed_stage);
        CHECK(!failure.completed.illumination);
        CHECK(!failure.completed.denoising);
        CHECK(!failure.completed.restoration);
        CHECK(!failure.completed.contrast);
        CHECK(!failure.completed.sharpening);
        CHECK(!failure.completed.binarization);
        CHECK(!failure.warning_context.interpretation);
        if (acquired) {
            CHECK(file_contents(source) == "not an image");
        } else {
            CHECK(!std::filesystem::exists(source));
        }
        CHECK(!std::filesystem::exists(output));
        auto staging = output;
        staging += ".staging-0";
        CHECK(!std::filesystem::exists(staging));
    }
}
} // namespace docenhance::tests
