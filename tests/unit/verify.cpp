// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/verify.hpp"

#include "bundle_native.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/host/processor.hpp"
#include "docenhance/record/record.hpp"
#include "temporary_directory.hpp"
#include "verify_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <utility>
#include <vector>

namespace docenhance::tests {
void write_verified_source(const std::filesystem::path& input,
                           const std::vector<std::uint8_t>& encoded) {
    {
        std::ofstream writing{input, std::ios::binary};
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        writing.write(reinterpret_cast<const char*>(encoded.data()),
                      static_cast<std::streamsize>(encoded.size()));
    }
}

namespace {
// MSVC gives openmode a signed underlying type, so the flags are combined as unsigned values.
std::ios::openmode binary_with(std::ios::openmode extra) {
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    return static_cast<std::ios::openmode>(static_cast<unsigned>(std::ios::binary) |
                                           static_cast<unsigned>(extra));
}

} // namespace

TEST_CASE("A bundle agrees with the record it was published with", "[verify]") {
    host::Processor processor{pinned_context};
    const TemporaryDirectory temporary{"docenhance-verify"};
    const auto bundle = published_bundle(temporary, "run", processor);
    const auto expected =
        verified_expected_files(bundle, {record::image_name, record::record_name});
    const auto confirmed = read_back(bundle);
    REQUIRE(confirmed);
    CHECK(confirmed->run == std::string(32, 'a'));
    CHECK(confirmed->recorded == "2026-01-01T00:00:00Z");
    check_verified_files(*confirmed, expected);

    // A bundle is read where it is, not where it was written: every declared path is relative.
    const auto moved = temporary.path / "moved";
    std::filesystem::rename(bundle, moved);
    CHECK(read_back(moved));
}

TEST_CASE("A bundle that disagrees with its record is refused", "[verify]") {
    host::Processor processor{pinned_context};
    const TemporaryDirectory temporary{"docenhance-verify-refusal"};

    SECTION("an artifact that is not the one recorded") {
        const auto bundle = published_bundle(temporary, "tampered", processor);
        {
            std::ofstream appending{bundle / record::image_name, binary_with(std::ios::app)};
            appending.put('\0');
        }
        const auto refused = read_back(bundle);
        REQUIRE(!refused);
        CHECK(refused.error().code == core::ErrorCode::input);
    }
    SECTION("a declared artifact that is absent") {
        const auto bundle = published_bundle(temporary, "missing", processor);
        std::filesystem::remove(bundle / record::image_name);
        CHECK(!read_back(bundle));
    }
    SECTION("a file the record does not declare") {
        const auto bundle = published_bundle(temporary, "extra", processor);
        std::ofstream{bundle / "notes.txt", std::ios::binary}.put('x');
        CHECK(!read_back(bundle));
    }
    SECTION("a record that is not there at all") {
        const auto bundle = published_bundle(temporary, "recordless", processor);
        std::filesystem::remove(bundle / record::record_name);
        CHECK(!read_back(bundle));
    }
    SECTION("a record this build cannot read") {
        const auto bundle = published_bundle(temporary, "malformed", processor);
        std::ofstream{bundle / record::record_name, binary_with(std::ios::trunc)} << "{";
        CHECK(!read_back(bundle));
    }
    SECTION("a record past the bound it is read under") {
        const auto bundle = published_bundle(temporary, "oversized", processor);
        std::ofstream padding{bundle / record::record_name, binary_with(std::ios::trunc)};
        padding << std::string(record::record_max_bytes + 1, ' ');
        padding.close();
        CHECK(!read_back(bundle));
    }
}
} // namespace docenhance::tests

namespace docenhance::tests {
TEST_CASE(
    "Inactive and moved directory owners cannot borrow the current directory or foreign paths") {
    const TemporaryDirectory temporary{"docenhance-directory-ownership"};
    const auto file = temporary.path / "owned.txt";
    {
        std::ofstream write{file};
        write << "owned bytes";
    }
    fs::BundleDirectory const inactive;
    CHECK(!inactive.file(utf8_spelling(file)));
    CHECK(!inactive.child(utf8_spelling(temporary.path)));
    CHECK(!inactive.entries());
    auto opened = fs::BundleDirectory::open(temporary.path);
    REQUIRE(opened);
    auto owner = std::move(*opened);
    auto retained = std::move(owner);
    CHECK(retained.file("owned.txt"));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK(!owner.file(utf8_spelling(file)));
    CHECK(!owner.child(utf8_spelling(temporary.path)));
    CHECK(!owner.entries());
    for (const auto* const name : {"", ".", "..", "../owned.txt", "sub/owned.txt"}) {
        CHECK(!retained.file(name));
        CHECK(!retained.child(name));
    }
    const auto nul = std::string("owned.txt") + '\0' + "ignored";
    CHECK(!retained.file(nul));
    CHECK(!retained.file(utf8_spelling(file)));
    owner = std::move(retained);
    CHECK(owner.file("owned.txt"));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK(!retained.entries());
#ifndef _WIN32
    const auto literal = temporary.path / "literal\\byte";
    {
        std::ofstream write{literal};
        write << "literal filename";
    }
    CHECK(owner.file("literal\\byte"));
#else
    CHECK(!owner.file("owned.txt:alternate"));
    CHECK(!owner.file("sub\\owned.txt"));
#endif
}
} // namespace docenhance::tests

namespace docenhance::tests {
namespace {
record::RunContext invalid_context() {
    return {};
}
unsigned context_calls = 0;
record::RunContext counted_context() {
    ++context_calls;
    return {
        .identity = std::string(32, context_calls == 1 ? 'a' : 'b'),
        .recorded = context_calls == 1 ? "2026-01-01T00:00:00Z" : "2026-01-01T00:00:01Z",
    };
}
} // namespace
TEST_CASE("A reusable native processor observes one context per execution", "[host][bundle]") {
    context_calls = 0;
    host::Processor processor{counted_context};
    CHECK(context_calls == 0);
    const TemporaryDirectory temporary{"docenhance-run-context"};
    const auto first = published_bundle(temporary, "first", processor);
    const auto second = published_bundle(temporary, "second", processor);
    REQUIRE(context_calls == 2);
    const auto a = read_back(first);
    const auto b = read_back(second);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->run == std::string(32, 'a'));
    CHECK(b->run == std::string(32, 'b'));
    CHECK(a->recorded == "2026-01-01T00:00:00Z");
    CHECK(b->recorded == "2026-01-01T00:00:01Z");
}
} // namespace docenhance::tests

namespace docenhance::tests {
TEST_CASE("An invalid run context is refused before filesystem processing", "[host][bundle]") {
    app::Invocation raw_invocation;
    raw_invocation.command = app::Command::process;
    raw_invocation.operands = {"nonexistent-input.png"};
    raw_invocation.option(app::OptionId::out_dir) = "nonexistent-parent/result";
    const app::Invocation invocation = std::move(raw_invocation);
    auto request = app::prepare_process(invocation);
    REQUIRE(request);
    host::Processor processor{invalid_context};
    const auto refused = processor.process(*request, {});
    REQUIRE(!refused);
    CHECK(refused.error().error.code == core::ErrorCode::invariant);
    CHECK(refused.error().publication == core::Publication::not_started);
}
} // namespace docenhance::tests
