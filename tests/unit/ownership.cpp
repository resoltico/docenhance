// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "delivery_fixture.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/codec/encoded_png.hpp"
#include "docenhance/codec/png_output.hpp"
#include "docenhance/color/interpretation.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/execution_limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/host/processor.hpp"
#include "docenhance/host/verifier.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/record/record.hpp"
#include "png_depth_fixture.hpp"
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
#include <variant>
namespace docenhance::tests {
namespace {
template <typename T>
concept BorrowsBytes = requires(T&& owner) { std::forward<T>(owner).bytes(); };
template <typename T>
concept BorrowsPlane = requires(T&& owner) { std::forward<T>(owner).view(); };
template <typename T>
concept BorrowsRequest = requires(T&& owner) { std::forward<T>(owner).input(); };
template <typename T>
concept BorrowsRaster = requires(T&& owner, image::InterpretationPolicy policy,
                                 image::PngShape output, core::Budget& budget) {
    color::Interpretation::create(std::forward<T>(owner), policy, output, budget);
};
template <typename T>
concept BorrowsInterpretationReader = requires(T&& owner) { std::forward<T>(owner).reader(); };
template <typename T>
concept BorrowsReaderScratch = requires(T&& owner) { std::forward<T>(owner).scratch(); };
static_assert(BorrowsInterpretationReader<color::Interpretation&>);
static_assert(!BorrowsInterpretationReader<color::Interpretation>);
static_assert(BorrowsReaderScratch<color::InterpretationReader&>);
static_assert(!BorrowsReaderScratch<color::InterpretationReader>);
static_assert(!std::is_copy_constructible_v<color::InterpretationReader>);
static_assert(std::is_nothrow_move_constructible_v<color::InterpretationReader>);
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
template <typename T>
concept BorrowsPlan = requires(T&& owner) { std::forward<T>(owner).plan(); };
template <typename T>
concept CarriesPhotometric = requires(T plan) { plan.photometric; };
template <typename T>
concept CarriesProtection = requires(T plan) { plan.protection; };
static_assert(CarriesPhotometric<methods::Plan> && CarriesProtection<methods::Plan>);
static_assert(!CarriesPhotometric<methods::BinaryOutput> &&
              !CarriesProtection<methods::BinaryOutput>);
static_assert(!CarriesPhotometric<methods::ContinuousOutput> &&
              !CarriesProtection<methods::ContinuousOutput>);
static_assert(BorrowsPlan<const app::ProcessRequest&> && !BorrowsPlan<app::ProcessRequest>);
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
                               const app::ExecutionControl& /*control*/) override {
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
TEST_CASE(
    "First-party published results transfer unchanged across independent invocation selections") {
    UnusedVerifier verifier;
    app::Invocation request;
    request.command = app::Command::process;
    request.operands = {"input.png"};
    request.option(app::OptionId::out_dir) = "output";
    request.option(app::OptionId::output_mode) = "bw";
    request.option(app::OptionId::binarize) = "fixed";
    const auto admitted = app::prepare_process(request);
    REQUIRE(admitted);
    const auto healthy = delivery_publication(*admitted);
    ReportingProcessor control{healthy};
    REQUIRE(app::dispatch(request, control, verifier).exit_code() == core::ExitCode::success);
    for (unsigned invalid = 0; invalid < 5; ++invalid) {
        auto reported = healthy;
        auto invocation = request;
        if (invalid == 0) {
            reported.record.context.identity.clear();
        } else if (invalid == 1) {
            invocation.option(app::OptionId::output_mode) = "preserve";
            invocation.option(app::OptionId::binarize).reset();
        } else if (invalid == 2) {
            reported.record.execution.binarization.reset();
        } else if (invalid == 3) {
            invocation.option(app::OptionId::illumination) = "surface";
        } else {
            invocation.option(app::OptionId::denoise) = "nlm";
        }
        REQUIRE(app::prepare_process(invocation));
        const bool valid_record = invalid != 0 && invalid != 2;
        CHECK(record::serialize(reported.record).has_value() == valid_record);
        const auto expected = reported;
        ReportingProcessor processor{std::move(reported)};
        const auto outcome = app::dispatch(invocation, processor, verifier);
        REQUIRE(outcome.exit_code() == core::ExitCode::success);
        REQUIRE(std::holds_alternative<app::Published>(outcome.payload));
        const auto& transferred = std::get<app::Published>(outcome.payload);
        CHECK(transferred.output == expected.output);
        CHECK(transferred.record_identity == expected.record_identity);
        CHECK(transferred.record == expected.record);
        CHECK(transferred.diagnostics == expected.diagnostics);
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
    app::Invocation invocation;
    invocation.command = app::Command::process;
    invocation.operands = {"input.png"};
    invocation.option(app::OptionId::out_dir) = "output";
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
    host::Processor processor;
    const auto control = app::ExecutionControl::create({}, core::default_memory_limit, stopped);
    REQUIRE(control);
    const auto refused = processor.process(original, *control);
    REQUIRE(!refused);
    CHECK(refused.error().error.code == core::ErrorCode::argument);
    CHECK(refused.error().publication == core::Publication::not_started);
    invocation.command = app::Command::verify;
    invocation.option(app::OptionId::out_dir).reset();
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
}
TEST_CASE(
    "PNG encoding validates borrowed-plane extents before charged allocation and preserves input") {
    core::Budget budget{std::size_t{1024} * 1024};
    const std::array<std::uint8_t, 4> samples{0, 1, 1, 0};
    const auto original = samples;
    const auto view = image::PlaneView<const std::uint8_t>::create(
                          samples, {.width = 2, .height = 2, .stride = 2})
                          .value();
    const codec::PngImage intended{
        .shape = {.width = 2, .height = 2, .depth = fixture_png_depth(1)},
        .pixels = view,
        .profile = std::span<const std::byte>(),
        .resolution = std::nullopt,
    };
    std::array<codec::PngImage, 3> invalid{intended, intended, intended};
    invalid.at(0).shape.height = 1;
    invalid.at(1).shape.width = 1;
    invalid.at(2).pixels = image::PlaneView<const std::uint8_t>();
    for (const auto& candidate : invalid) {
        const auto refused = codec::encode_png(candidate, budget);
        REQUIRE(!refused);
        CHECK(refused.error().code == core::ErrorCode::argument);
        CHECK(samples == original);
        CHECK(budget.used() == 0);
        CHECK(budget.peak() == 0);
    }
    {
        const auto encoded = codec::encode_png(intended, budget);
        REQUIRE(encoded);
        CHECK(!encoded->bytes().empty());
        REQUIRE(codec::compare_png(encoded->bytes(), intended, budget));
        CHECK(samples == original);
    }
    CHECK(budget.used() == 0);
    CHECK(budget.peak() > 0);
}
} // namespace docenhance::tests
