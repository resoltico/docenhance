// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "bundle_native.hpp"
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/bundle.hpp"
#include "temporary_directory.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#ifndef _WIN32
#include <sys/stat.h>
#endif
namespace docenhance::tests {
TEST_CASE("Bundle native access refuses links and special files at opening", "[bundle][io]") {
    const TemporaryDirectory temporary{"docenhance-bundle-native"};
    const auto root = temporary.path / "bundle";
    REQUIRE(std::filesystem::create_directory(root));
    auto directory = io::BundleDirectory::open(root);
    REQUIRE(directory);
    std::ofstream{temporary.path / "outside"} << "outside";
    std::error_code error;
    std::filesystem::create_symlink(temporary.path / "outside", root / "run.json", error);
    if (!error) {
        CHECK(!directory->file("run.json"));
    }
    REQUIRE(std::filesystem::create_directory(root / "result.png"));
    CHECK(!directory->file("result.png"));
#ifndef _WIN32
    constexpr unsigned int owner_read_write = 0600;
    REQUIRE(mkfifo((root / "pipe").c_str(), owner_read_write) == 0);
    CHECK(!directory->file("pipe"));
#endif
}
TEST_CASE("Bundle native reads retain opened identity across pathname replacement",
          "[bundle][io]") {
    const TemporaryDirectory temporary{"docenhance-bundle-replacement"};
    const auto root = temporary.path / "bundle";
    REQUIRE(std::filesystem::create_directory(root));
    std::ofstream{root / "run.json"} << "owned";
    auto directory = io::BundleDirectory::open(root);
    REQUIRE(directory);
    auto file = directory->file("run.json");
    REQUIRE(file);
#ifndef _WIN32
    std::filesystem::rename(root / "run.json", root / "original");
    std::filesystem::create_symlink(root / "original", root / "run.json");
    CHECK(!directory->file("run.json"));
    const auto retained = temporary.path / "retained";
    std::filesystem::rename(root, retained);
    REQUIRE(std::filesystem::create_directory(root));
    std::ofstream{root / "original"} << "foreign";
    CHECK(directory->file("original"));
#else
    std::error_code error;
    std::filesystem::rename(root / "run.json", root / "original", error);
    CHECK(error); // The open handle denies replacement on Windows.
#endif
    std::array<char, 5> bytes{};
    REQUIRE(std::fread(bytes.data(), 1, bytes.size(), file->get()) == bytes.size());
    CHECK(std::string(bytes.data(), bytes.size()) == "owned");
}
TEST_CASE("Bundle reading and hashing observe bounded cancellation", "[bundle][cancellation]") {
    const TemporaryDirectory temporary{"docenhance-bundle-stop"};
    const auto root = temporary.path / "bundle";
    REQUIRE(std::filesystem::create_directory(root));
    constexpr std::size_t blocks = std::size_t{3} * 64 * 1024;
    std::ofstream{root / "run.json"} << std::string(blocks, 'x');
    const CheckpointStop stop{core::Checkpoint::verification, 1};
    core::Budget budget{io::bundle_snapshot_budget};
    auto read = io::read_bundle(utf8_spelling(root), budget, stop.cancellation(),
                                io::bundle_max_file_bytes);
    REQUIRE(!read);
    CHECK(read.error().code == core::ErrorCode::cancelled);
    CHECK(budget.used() == 0);
}
} // namespace docenhance::tests
