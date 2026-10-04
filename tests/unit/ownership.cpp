// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/color/converter.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/host/processor.hpp"
#include "docenhance/host/verifier.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "stub_verifier.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <future>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <type_traits>
#include <utility>
namespace docenhance::tests {
namespace {
template <typename T>
concept BorrowsBytes = requires(T&& owner) { std::forward<T>(owner).bytes(); };
template <typename T>
concept BorrowsPlane = requires(T&& owner) { std::forward<T>(owner).view(); };
template <typename T>
concept BorrowsRequest = requires(T&& owner) { std::forward<T>(owner).input(); };
template <typename T>
concept BorrowsRaster = requires(T&& owner, image::Continuous operation, core::Budget& budget) {
    color::Converter::create(std::forward<T>(owner), operation, budget);
};
template <typename T>
concept BorrowsPng = requires(T&& owner) { std::forward<T>(owner).png(); };
static_assert(BorrowsPng<image::RasterMetadata&> && BorrowsPng<const image::RasterMetadata&>);
static_assert(!BorrowsPng<image::RasterMetadata> && !BorrowsPng<const image::RasterMetadata>);
struct Task {
    core::Result<void> operator()(std::size_t /*index*/) const {
        return {};
    }
};
static_assert(BorrowsBytes<core::Buffer&> && BorrowsBytes<const core::Buffer&>);
static_assert(!BorrowsBytes<core::Buffer> && !BorrowsBytes<const core::Buffer>);
using Plane = image::Plane<std::uint8_t>;
static_assert(BorrowsPlane<Plane&> && BorrowsPlane<const Plane&>);
static_assert(!BorrowsPlane<Plane> && !BorrowsPlane<const Plane>);
static_assert(BorrowsRequest<app::ProcessRequest&> && !BorrowsRequest<app::ProcessRequest>);
static_assert(BorrowsRaster<const image::Raster&> && !BorrowsRaster<image::Raster>);
static_assert(!BorrowsRaster<const image::Raster>);
static_assert(std::is_constructible_v<exec::WorkRef, Task&>);
static_assert(std::is_constructible_v<exec::WorkRef, const Task&>);
static_assert(!std::is_constructible_v<exec::WorkRef, Task>);
static_assert(std::is_copy_constructible_v<exec::WorkRef>);

class ReportingProcessor final : public app::Processor {
  public:
    explicit ReportingProcessor(app::Published result) : result_(std::move(result)) {}
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const core::Cancellation& /*cancellation*/) override {
        return result_;
    }

  private:
    app::Published result_;
};
} // namespace
TEST_CASE("Ledger arithmetic admits SIZE_MAX and rejects another charge without wrapping") {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    core::Budget budget{maximum};
    auto lease = budget.reserve(maximum).value();
    REQUIRE(budget.used() == maximum);
    REQUIRE(budget.available() == 0);
    REQUIRE(!budget.reserve(1));
    REQUIRE(!budget.allocate(1));
    REQUIRE(budget.allocate(0)->empty());
    lease = {};
    REQUIRE(budget.available() == maximum);
    auto first = budget.reserve(maximum - 1).value();
    auto last = budget.reserve(1).value();
    first = std::move(last);
    REQUIRE(budget.used() == 1);
    first = {};
    REQUIRE(budget.used() == 0);
}
TEST_CASE("Concurrent buffers and reservations share one ledger and refund every charge") {
    core::Budget budget{1024};
    const auto worker = [&budget] {
        for (unsigned i = 0; i < 100; ++i) {
            auto bytes = budget.allocate(11).value();
            const auto reservation = budget.reserve(7).value();
            bytes.bytes().front() = std::byte{42};
        }
    };
    std::array<std::future<void>, 4> workers;
    for (auto& future : workers) {
        future = std::async(std::launch::async, worker);
    }
    for (auto& future : workers) {
        future.get();
    }
    REQUIRE(budget.used() == 0);
}
TEST_CASE("Wrong-family and incomplete processing reports preserve unknown publication") {
    UnusedVerifier verifier;
    contract::Invocation request;
    request.command = contract::Command::process;
    request.subject = "input.png";
    request.output_directory = "output";
    ReportingProcessor binary{
        app::PublishedBinary{.output = "output/result.png", .run = {}, .record = {}}};
    REQUIRE(app::dispatch(request, binary, verifier).exit_code() ==
            core::ExitCode::publication_unknown);
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        app::PublishedContinuous reported{};
        reported.output = "output/result.png";
        reported.conversion.verified = invalid != 1;
        reported.illumination.complete = invalid != 2;
        reported.denoising.complete = invalid != 3;
        ReportingProcessor continuous{reported};
        request.output_mode = invalid == 0 ? "bw" : "preserve";
        const auto outcome = app::dispatch(request, continuous, verifier);
        REQUIRE(outcome.exit_code() == core::ExitCode::publication_unknown);
        REQUIRE(std::get<app::ProcessFailure>(outcome.payload).error.publication ==
                core::Publication::unknown);
    }
}
TEST_CASE("Percentile selection borrows scratch and refuses invalid input before reordering") {
    std::array<double, 5> values{9, 1, 3, 3, 7};
    const auto original = values;
    REQUIRE(!image::nearest_rank(values, -1));
    REQUIRE(values == original);
    REQUIRE(!image::nearest_rank(values, std::numeric_limits<double>::quiet_NaN()));
    REQUIRE(values == original);
    REQUIRE(image::nearest_rank(values, 0.5).value() == 3);
    values = original;
    values.back() = std::numeric_limits<double>::infinity();
    const auto invalid = values;
    REQUIRE(!image::nearest_rank(values, 0.5));
    REQUIRE(values == invalid);
}
} // namespace docenhance::tests

namespace docenhance::tests {
TEST_CASE("Copying a mutable work reference retains the callable rather than the wrapper") {
    std::size_t calls = 0;
    std::size_t other_calls = 0;
    const auto replacement = [&other_calls](std::size_t /*index*/) -> core::Result<void> {
        ++other_calls;
        return {};
    };
    const auto task = [&calls](std::size_t /*index*/) -> core::Result<void> {
        ++calls;
        return {};
    };
    std::optional<exec::WorkRef> copied;
    {
        exec::WorkRef first{task};
        copied.emplace(first);
        first = exec::WorkRef{replacement};
        REQUIRE((*copied)(0));
        REQUIRE(calls == 1);
        REQUIRE(other_calls == 0);
    }
    REQUIRE((*copied)(0));
    REQUIRE(calls == 2);
    REQUIRE(other_calls == 0);
}
} // namespace docenhance::tests

namespace docenhance::tests {
TEST_CASE("Moved requests become unready and native ports refuse them before cancellation") {
    contract::Invocation invocation;
    invocation.command = contract::Command::process;
    invocation.subject = "input.png";
    invocation.output_directory = "output";
    auto original = app::prepare_process(invocation).value();
    auto moved = std::move(original);
    CHECK(moved.ready());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK(!original.ready());
    auto replacement = app::prepare_process(invocation).value();
    replacement = std::move(moved);
    CHECK(replacement.ready());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK(!moved.ready());
    std::stop_source source;
    static_cast<void>(source.request_stop());
    const core::Cancellation stopped{source.get_token()};
    source = std::stop_source{std::nostopstate};
    CHECK(stopped.requested(core::Checkpoint::admission));
    CHECK(!source.stop_possible());
    host::Processor processor{{.identity = "unused", .recorded = "unused"}};
    const auto refused = processor.process(original, stopped);
    REQUIRE(!refused);
    CHECK(refused.error().error.code == core::ErrorCode::argument);
    CHECK(refused.error().error.publication == core::Publication::not_started);
    invocation.command = contract::Command::verify;
    auto reading = app::prepare_verify(invocation).value();
    auto retained = std::move(reading);
    CHECK(retained.ready());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK(!reading.ready());
    reading = std::move(retained);
    CHECK(reading.ready());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK(!retained.ready());
    host::Verifier verifier;
    const auto denied = verifier.verify(retained, stopped);
    REQUIRE(!denied);
    CHECK(denied.error().code == core::ErrorCode::argument);
    CHECK(denied.error().publication == core::Publication::not_started);
}
TEST_CASE("Binary row adapters validate bounds before borrowing and preserve rejected output") {
    const std::array<std::uint8_t, 4> samples{1, 2, 3, 4};
    const auto view = image::PlaneView<const std::uint8_t>::create(
                          samples, {.width = 4, .height = 1, .stride = 4})
                          .value();
    image::PlaneRows rows{view};
    std::array<std::uint8_t, 4> output{9, 9, 9, 9};
    const auto original = output;
    CHECK(!rows.row(1, output, image::RowUse::output));
    CHECK(
        !rows.row(std::numeric_limits<std::uint32_t>::max(), output, image::RowUse::verification));
    CHECK(!rows.row(0, std::span{output}.first(1), image::RowUse::output));
    CHECK(output == original);
    CHECK(rows.row(0, output, image::RowUse::output));
    CHECK(output == samples);
    image::PlaneRows empty{{}};
    CHECK(!empty.row(0, output, image::RowUse::output));
}
} // namespace docenhance::tests
