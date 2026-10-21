// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "bundle_stage.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/publication.hpp"
#include "file_contents.hpp"
#include "native_publication.hpp"
#include "temporary_directory.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <system_error>
namespace docenhance::tests {
TEST_CASE("Bundle cleanup preserves replacement files and directories", "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-ownership"};
    const auto output = temporary.path / "result";
    const auto stage_path = temporary.path / "result.staging-0";
    {
        fs::Stage stage;
        REQUIRE(fs::reserve_stage(stage, output, {}));
        const auto file = fs::place(stage, "run.json");
        REQUIRE(file);
        const auto& slot = *file;
        REQUIRE(fs::write_bytes(slot, std::as_bytes(std::span{std::string_view{"owned"}}), {}));
        SECTION("a different object occupies the known file name") {
            std::filesystem::rename(file->path, stage_path / "retained-original");
            std::ofstream{file->path} << "foreign";
        }
        SECTION("a different directory occupies the stage name") {
            std::filesystem::rename(stage_path, temporary.path / "retained-stage");
            REQUIRE(std::filesystem::create_directory(stage_path));
            std::ofstream{stage_path / "run.json"} << "foreign";
        }
        const auto failure =
            fs::abandon(stage, {.code = core::ErrorCode::output, .message = "Stop before commit"});
        CHECK(failure.publication == core::Publication::unknown);
        CHECK(stage.retained);
    }
    CHECK(file_contents(stage_path / "run.json") == "foreign");
}
TEST_CASE("Bundle cleanup refuses a link replacement without touching its target",
          "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-cleanup-link"};
    const auto outside = temporary.path / "outside";
    std::ofstream{outside} << "preserve";
    fs::Stage stage;
    REQUIRE(fs::reserve_stage(stage, temporary.path / "result", {}));
    const auto file = fs::place(stage, "run.json");
    REQUIRE(file);
    const auto& slot = *file;
    REQUIRE(fs::write_bytes(slot, std::as_bytes(std::span{std::string_view{"owned"}}), {}));
    std::filesystem::rename(file->path, stage.directory / "retained-original");
    std::error_code error;
    std::filesystem::create_symlink(outside, file->path, error);
    REQUIRE(!error);
    const auto failure =
        fs::abandon(stage, {.code = core::ErrorCode::output, .message = "Stop before commit"});
    CHECK(failure.publication == core::Publication::unknown);
    CHECK(std::filesystem::is_symlink(file->path));
    CHECK(file_contents(outside) == "preserve");
}
TEST_CASE("Bundle cleanup does not traverse a replaced assets directory", "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-cleanup-assets"};
    fs::Stage stage;
    REQUIRE(fs::reserve_stage(stage, temporary.path / "result", {}));
    const auto file = fs::place(stage, "assets/protect-mask.png");
    REQUIRE(file);
    const auto& slot = *file;
    REQUIRE(fs::write_bytes(slot, std::as_bytes(std::span{std::string_view{"owned"}}), {}));
    const auto retained = temporary.path / "retained-assets";
    std::filesystem::rename(stage.directory / "assets", retained);
    std::error_code error;
    std::filesystem::create_directory_symlink(retained, stage.directory / "assets", error);
    REQUIRE(!error);
    const auto failure =
        fs::abandon(stage, {.code = core::ErrorCode::output, .message = "Stop before commit"});
    CHECK(failure.publication == core::Publication::unknown);
    CHECK(file_contents(retained / "protect-mask.png") == "owned");
    CHECK(std::filesystem::is_symlink(stage.directory / "assets"));
}
TEST_CASE("Bundle staging reuses only retained owned directories", "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-shared-parent"};
    fs::Stage stage;
    REQUIRE(fs::reserve_stage(stage, temporary.path / "result", {}));
    const auto first = fs::place(stage, "assets/first.png");
    REQUIRE(first);
    REQUIRE(fs::write_bytes(*first, std::as_bytes(std::span{std::string_view{"first"}}), {}));
    const auto second = fs::place(stage, "assets/second.png");
    REQUIRE(second);
    REQUIRE(fs::write_bytes(*second, std::as_bytes(std::span{std::string_view{"second"}}), {}));
    CHECK(stage.created.size() == 3);
    CHECK(stage.owns_entries());
    CHECK(stage.cleanup());
    CHECK(!std::filesystem::exists(stage.directory));

    fs::Stage foreign;
    REQUIRE(fs::reserve_stage(foreign, temporary.path / "foreign", {}));
    REQUIRE(std::filesystem::create_directory(foreign.directory / "assets"));
    CHECK(!fs::place(foreign, "assets/first.png"));
    CHECK(std::filesystem::is_directory(foreign.directory / "assets"));
}
} // namespace docenhance::tests
