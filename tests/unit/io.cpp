// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/continuous_png.hpp"
#include "docenhance/codec/png.hpp"
#include "docenhance/codec/png_output.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/fs/paths.hpp"
#include "docenhance/fs/publication.hpp"
#include "docenhance/fs/snapshot.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "native_publication.hpp"
#include "png_fixture.hpp"
#include "png_transaction.hpp"
#include "temporary_directory.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace docenhance::tests {
namespace {
constexpr std::size_t mebibyte = std::size_t{1024} * 1024;
core::Result<image::Raster> decode_snapshot(const fs::Snapshot& snapshot, core::Budget& budget) {
    auto typed = image::Plane<std::uint8_t>::allocate(
        budget, static_cast<std::uint32_t>(snapshot.encoded.size()), 1);
    if (!typed) {
        return std::unexpected(std::move(typed.error()));
    }
    std::ranges::transform(snapshot.encoded.bytes(), typed->view().row(0).begin(),
                           [](std::byte byte) { return std::to_integer<std::uint8_t>(byte); });
    return codec::decode_png_raster(typed->view().row(0), budget, image::ProfilePolicy::embedded);
}
// A synchronous FS checkpoint observes actual completed writes without changing the stage.
// State is reset before publication; this fixture never runs concurrently with another call.
class PriorBundleWrites {
  public:
    explicit PriorBundleWrites(const std::filesystem::path& directory) {
        stage = directory;
        observed = false;
        observation_failed = false;
    }
    static bool observe(core::Checkpoint at) noexcept {
        if (at != core::Checkpoint::staging) {
            return false;
        }
        try {
            std::ifstream first{stage / "result.png", std::ios::binary};
            std::ifstream second{stage / "assets" / "protect-mask.png", std::ios::binary};
            first.seekg(0, std::ios::end);
            second.seekg(0, std::ios::end);
            observed =
                observed || (first.good() && second.good() && first.tellg() == std::streampos{5} &&
                             second.tellg() == std::streampos{4});
        } catch (...) {
            observation_failed = true;
        }
        return false;
    }
    [[nodiscard]] static core::Cancellation cancellation() noexcept {
        return core::Cancellation{{}, observe};
    }
    inline static std::filesystem::path stage;
    inline static bool observed = false, observation_failed = false;
};
void check_snapshot_decode(const fs::Snapshot& snapshot, core::Budget& constrained,
                           std::size_t limit) {
    const auto decoded = decode_snapshot(snapshot, constrained);
    if (!decoded) {
        CHECK(decoded.error().code == core::ErrorCode::resource);
    }
    if (limit == mebibyte) {
        REQUIRE(decoded);
    }
}
void check_source_budget(const std::filesystem::path& original, core::Budget& constrained,
                         std::size_t limit) {
    const auto snapshot = fs::read_source_snapshot(utf8_spelling(original / "result.png"),
                                                   constrained, {}, codec::png_max_encoded_bytes);
    if (limit == mebibyte) {
        REQUIRE(snapshot);
    }
    if (snapshot) {
        check_snapshot_decode(*snapshot, constrained, limit);
    } else {
        CHECK(snapshot.error().code == core::ErrorCode::resource);
    }
    if (limit == 0) {
        CHECK(!snapshot);
    }
}
void check_codec_publication_refusal(const fs::PublishResult<std::string>& result,
                                     const std::filesystem::path& output) {
    if (!result) {
        CHECK(result.error().error.code == core::ErrorCode::resource);
        CHECK(result.error().publication == core::Publication::not_started);
        CHECK(!std::filesystem::exists(output));
        CHECK(!std::filesystem::exists(utf8_spelling(output) + ".staging-0"));
    }
}
void check_codec_budget(const std::filesystem::path& original, const std::filesystem::path& output,
                        image::PlaneView<const std::uint8_t> pixels, std::size_t limit) {
    core::Budget constrained{limit};
    check_source_budget(original, constrained, limit);
    CHECK(constrained.used() == 0);
    const auto result = publish_png_fixture(utf8_spelling(output), binary_png(pixels), constrained);
    if (limit == mebibyte) {
        REQUIRE(result);
    }
    check_codec_publication_refusal(result, output);
    CHECK(constrained.used() == 0);
}
} // namespace

TEST_CASE("Codec allocations share the caller budget and refund on every path", "[io]") {
    const TemporaryDirectory temporary{"docenhance-io"};
    core::Budget source_budget{mebibyte};
    auto image = image::Plane<std::uint8_t>::allocate(source_budget, 2, 1);
    REQUIRE(image);
    image->view().row(0).front() = 0;
    image->view().row(0).back() = 1;
    const auto charged = source_budget.used();
    const auto original = temporary.path / "original";
    REQUIRE(publish_png_fixture(utf8_spelling(original), binary_png(image->view().as_const()),
                                source_budget));
    CHECK(source_budget.used() == charged);
    constexpr auto limits = std::to_array<std::size_t>({0, 64, 1024, 4096, 65536, mebibyte});
    for (const auto limit : limits) {
        const auto output = temporary.path / ("budget-" + std::to_string(limit));
        check_codec_budget(original, output, image->view().as_const(), limit);
    }
}

TEST_CASE("Binary output is read back and compared before it is published", "[io][png]") {
    const TemporaryDirectory temporary{"docenhance-binary-verify"};
    core::Budget budget{mebibyte};
    auto image = image::Plane<std::uint8_t>::allocate(budget, 8, 4);
    REQUIRE(image);
    for (std::uint32_t y = 0; y < image->height(); ++y) {
        std::ranges::fill(image->view().row(y), y % 2 == 0 ? std::uint8_t{0} : std::uint8_t{1});
    }
    const auto published = temporary.path / "verified";
    REQUIRE(publish_png_fixture(utf8_spelling(published), binary_png(image->view().as_const()),
                                budget));

    // What the verifier compares against, and what it does when the file disagrees with it. A
    // single flipped byte in the final chunk framing must be refused.
    const auto result = published / "result.png";
    std::string bytes;
    {
        std::ifstream reading{result, std::ios::binary};
        bytes.assign(std::istreambuf_iterator<char>{reading}, std::istreambuf_iterator<char>{});
    }
    REQUIRE(bytes.size() > 40);
    const auto tampered = temporary.path / "tampered.png";
    {
        std::string damaged = bytes;
        auto& byte = damaged.at(damaged.size() - 12);
        byte = static_cast<char>(static_cast<std::uint8_t>(byte) ^ 0xFFU);
        std::ofstream writing{tampered, std::ios::binary};
        writing.write(damaged.data(), static_cast<std::streamsize>(damaged.size()));
    }
    const auto healthy = fs::read_source_snapshot(utf8_spelling(result), budget, {}, mebibyte);
    REQUIRE(healthy);
    CHECK(
        codec::compare_png(healthy->encoded.bytes(), binary_png(image->view().as_const()), budget));
    CHECK(std::to_integer<unsigned>(healthy->encoded.bytes()[24]) == 1);
    const auto damaged = fs::read_source_snapshot(utf8_spelling(tampered), budget, {}, mebibyte);
    REQUIRE(damaged);
    const auto refused =
        codec::compare_png(damaged->encoded.bytes(), binary_png(image->view().as_const()), budget);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::output_verify);
}

TEST_CASE("A bundle is published whole or not at all", "[io]") {
    const TemporaryDirectory temporary{"docenhance-bundle"};
    constexpr std::string_view image_bytes = "image";
    constexpr std::string_view mask_bytes = "mask";
    constexpr std::string_view record_bytes = "{}";
    std::array files{
        fs::BundleFile{.relative = "result.png", .bytes = std::as_bytes(std::span{image_bytes})},
        fs::BundleFile{
            .relative = "assets/protect-mask.png",
            .bytes = std::as_bytes(std::span{mask_bytes}),
        },
        fs::BundleFile{.relative = "run.json", .bytes = std::as_bytes(std::span{record_bytes})},
    };
    const auto complete = temporary.path / "complete";
    REQUIRE(fs::publish_bundle(utf8_spelling(complete), files, {}, fs::rename_exclusive));
    CHECK(std::filesystem::is_regular_file(complete / "result.png"));
    CHECK(std::filesystem::is_regular_file(complete / "assets" / "protect-mask.png"));
    CHECK(std::filesystem::is_regular_file(complete / "run.json"));

    const auto parent = temporary.path / "negative";
    REQUIRE(std::filesystem::create_directory(parent));
    const auto foreign = parent / "unrelated";
    REQUIRE(std::filesystem::create_directory(foreign));
    // Both an already-owned directory and an already-owned regular file refuse a third slot.
    // The real first two writes precede the duplicate guard; this is not cold table admission.
    for (const auto* const repeated : {"assets", "result.png"}) {
        files.back().relative = repeated;
        const auto output = parent / "refused";
        const PriorBundleWrites observation{parent / "refused.staging-0"};
        const auto published = fs::publish_bundle(
            utf8_spelling(output), files, PriorBundleWrites::cancellation(), fs::rename_exclusive);
        REQUIRE(!published);
        REQUIRE(PriorBundleWrites::observed);
        CHECK(!PriorBundleWrites::observation_failed);
        CHECK(published.error().error.code == core::ErrorCode::output);
        CHECK(published.error().publication == core::Publication::not_published);
        CHECK(!std::filesystem::exists(output));
        CHECK(!std::filesystem::exists(parent / "refused.staging-0"));
        CHECK(std::filesystem::is_directory(foreign));
        std::size_t entries = 0;
        for (const auto& entry : std::filesystem::directory_iterator{parent}) {
            CHECK(entry.path() == foreign);
            ++entries;
        }
        CHECK(entries == 1);
    }
    CHECK(std::filesystem::is_regular_file(complete / "run.json"));
}

TEST_CASE("The native commit refuses even an existing empty directory", "[io]") {
    const TemporaryDirectory temporary{"docenhance-io"};
    const auto source = temporary.path / "source";
    const auto target = temporary.path / "target";
    REQUIRE(std::filesystem::create_directory(source));
    REQUIRE(std::filesystem::create_directory(target));
    const auto error = fs::rename_exclusive(source, target);
    CHECK(error);
    CHECK(fs::definitely_not_published(error));
    CHECK(std::filesystem::is_directory(source));
    CHECK(std::filesystem::is_directory(target));
}

TEST_CASE("Only one concurrent native commit can win", "[io]") {
    const TemporaryDirectory temporary{"docenhance-io"};
    const auto first = temporary.path / "first";
    const auto second = temporary.path / "second";
    const auto target = temporary.path / "target";
    REQUIRE(std::filesystem::create_directory(first));
    REQUIRE(std::filesystem::create_directory(second));
    std::atomic<unsigned> ready = 0;
    std::atomic<bool> start = false;
    std::error_code first_error;
    std::error_code second_error;
    {
        std::jthread const a{[&] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            first_error = fs::rename_exclusive(first, target);
        }};
        std::jthread const b{[&] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            second_error = fs::rename_exclusive(second, target);
        }};
        while (ready.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
        start.store(true, std::memory_order_release);
    }
    CHECK((first_error.value() == 0) != (second_error.value() == 0));
    CHECK(std::filesystem::is_directory(target));
    CHECK(std::filesystem::exists(first) != std::filesystem::exists(second));
}
TEST_CASE("File and memory PNG sources use identical decoding and error policy", "[io][png]") {
    const TemporaryDirectory temporary{"docenhance-io"};
    const auto input = temporary.path / "parity.png";
    const GrayFixture fixture{
        .width = 2,
        .height = 1,
        .depth = 2,
        .interlaced = true,
        .samples = {0, 3},
    };
    auto bytes = make_gray_png(fixture);
    for (const bool corrupt : {false, true}) {
        if (corrupt) {
            bytes.at(29) ^= 1U;
        }
        {
            std::ofstream stream{input, std::ios::binary};
            for (const auto byte : bytes) {
                stream.put(static_cast<char>(byte));
            }
            stream.close();
            REQUIRE(stream);
        }
        core::Budget file_budget{mebibyte};
        core::Budget memory_budget{mebibyte};
        {
            const auto snapshot =
                fs::read_source_snapshot(utf8_spelling(input), file_budget, {}, mebibyte);
            REQUIRE(snapshot);
            // Decode the same owned snapshot, never reopen the mutable pathname.
            const auto from_file = decode_snapshot(*snapshot, file_budget);
            const auto from_memory =
                codec::decode_png_raster(bytes, memory_budget, image::ProfilePolicy::embedded);
            REQUIRE(from_file.has_value() == from_memory.has_value());
            if (corrupt) {
                REQUIRE(!from_file);
                CHECK(from_file.error().code == from_memory.error().code);
                CHECK(from_file.error().code == core::ErrorCode::input);
            } else {
                REQUIRE(from_file);
                CHECK(from_file->shape == from_memory->shape);
                CHECK(from_file->shape.width == fixture.width);
                CHECK(from_file->shape.height == fixture.height);
                CHECK(from_file->shape.model == image::SampleModel::gray);
                CHECK(from_file->shape.depth == image::SampleDepth::byte());
                CHECK(std::ranges::equal(from_file->pixels.view().row(0),
                                         from_memory->pixels.view().row(0)));
                CHECK(from_file->pixels.view().row(0).front() == 0);
                CHECK(from_file->pixels.view().row(0).back() == 255);
                CHECK(snapshot->identity.sha256.size() == core::sha256_hex_length);
                CHECK(snapshot->identity.bytes == bytes.size());
            }
        }
        CHECK(file_budget.used() == 0);
        CHECK(memory_budget.used() == 0);
    }
}

TEST_CASE("A recorded name keeps the bytes the path was admitted with", "[io]") {
    // A document's name is the one part of a bundle that carries the original's own characters,
    // and a platform that spells paths in wide characters must not hand them back through an
    // encoding that cannot express them.
    const std::string admitted = "dokuments-\u0101-\u6587.png";
    CHECK(fs::file_name("folder/" + admitted) == admitted);
    CHECK(fs::file_name(admitted) == admitted);
    // A path that names no file at all is reported as it was given, not as an empty name.
    CHECK(fs::file_name("").empty());
}
} // namespace docenhance::tests

namespace docenhance::tests {
TEST_CASE("Digest adapter matches the SHA-256 abc known answer", "[io][digest]") {
    constexpr std::array<std::byte, 3> input{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    const auto identity = fs::identify(input);
    REQUIRE(identity);
    CHECK(identity->bytes == input.size());
    CHECK(identity->sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
} // namespace docenhance::tests
