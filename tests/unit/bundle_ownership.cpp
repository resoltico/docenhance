// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "bundle_stage.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/bundle.hpp"
#include "publication.hpp"
#include "temporary_directory.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
namespace docenhance::tests {
TEST_CASE("Bundle cleanup preserves replacement files and directories", "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-ownership"};
    const auto output = temporary.path / "result";
    const auto stage_path = temporary.path / "result.staging-0";
    {
        io::Stage stage;
        REQUIRE(io::reserve_stage(stage, output, {}));
        const auto file = io::place(stage, "run.json");
        REQUIRE(file);
        const io::BundleSlot slot{.path = *file, .created = &stage.created.front().identity};
        REQUIRE(io::write_bytes(slot, "owned"));
        SECTION("a different object occupies the known file name") {
            std::filesystem::rename(*file, stage_path / "retained-original");
            std::ofstream{*file} << "foreign";
        }
        SECTION("a different directory occupies the stage name") {
            std::filesystem::rename(stage_path, temporary.path / "retained-stage");
            REQUIRE(std::filesystem::create_directory(stage_path));
            std::ofstream{stage_path / "run.json"} << "foreign";
        }
        const auto failure =
            io::abandon(stage, {.code = core::ErrorCode::output, .message = "Stop before commit"});
        CHECK(failure.publication == core::Publication::unknown);
        CHECK(stage.retained);
    }
    CHECK(std::filesystem::file_size(stage_path / "run.json") == std::string("foreign").size());
}
TEST_CASE("Bundle cleanup refuses a link replacement without touching its target",
          "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-cleanup-link"};
    const auto outside = temporary.path / "outside";
    std::ofstream{outside} << "preserve";
    io::Stage stage;
    REQUIRE(io::reserve_stage(stage, temporary.path / "result", {}));
    const auto file = io::place(stage, "run.json");
    REQUIRE(file);
    const io::BundleSlot slot{.path = *file, .created = &stage.created.front().identity};
    REQUIRE(io::write_bytes(slot, "owned"));
    std::filesystem::rename(*file, stage.directory / "retained-original");
    std::error_code error;
    std::filesystem::create_symlink(outside, *file, error);
    if (error) {
        return;
    } // Windows link creation may require a privilege unavailable to tests.
    const auto failure =
        io::abandon(stage, {.code = core::ErrorCode::output, .message = "Stop before commit"});
    CHECK(failure.publication == core::Publication::unknown);
    CHECK(std::filesystem::is_symlink(*file));
    CHECK(std::filesystem::file_size(outside) == std::string("preserve").size());
}
TEST_CASE("Bundle cleanup does not traverse a replaced assets directory", "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-cleanup-assets"};
    io::Stage stage;
    REQUIRE(io::reserve_stage(stage, temporary.path / "result", {}));
    const auto file = io::place(stage, "assets/protect-mask.png");
    REQUIRE(file);
    const io::BundleSlot slot{.path = *file, .created = &stage.created.front().identity};
    REQUIRE(io::write_bytes(slot, "owned"));
    const auto retained = temporary.path / "retained-assets";
    std::filesystem::rename(stage.directory / "assets", retained);
    std::error_code error;
    std::filesystem::create_directory_symlink(retained, stage.directory / "assets", error);
    if (error) {
        return;
    }
    const auto failure =
        io::abandon(stage, {.code = core::ErrorCode::output, .message = "Stop before commit"});
    CHECK(failure.publication == core::Publication::unknown);
    CHECK(std::filesystem::file_size(retained / "protect-mask.png") == std::string("owned").size());
    CHECK(std::filesystem::is_symlink(stage.directory / "assets"));
}
} // namespace docenhance::tests
