// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/pipeline/render.hpp"

#include "docenhance/codec/png_output.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"
#include "support/render_receipt.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
namespace core = docenhance::core;
namespace image = docenhance::image;
namespace methods = docenhance::methods;
namespace pipeline = docenhance::pipeline;
using docenhance::fuzz::require;
docenhance::fuzz::RenderHarnessCounts& harness_counts() {
    static docenhance::fuzz::RenderHarnessCounts value;
    return value;
}
constexpr std::size_t header_size = 108;
constexpr std::array<std::uint8_t, 179> healthy_seed{
    0x01, 0x00, 0x00, 0x00, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfb, 0x9d, 0x5a,
    0x01, 0xf5, 0xf2, 0xc2, 0xd8, 0x12, 0x5c, 0xe8, 0x1e, 0x0b, 0xb7, 0xa4, 0x2d, 0x59, 0x0e,
    0xb4, 0x78, 0x01, 0x1b, 0xa2, 0x50, 0xe3, 0x24, 0x6d, 0xd9, 0x32, 0x0e, 0x10, 0x4b, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x57, 0xdd, 0x52, 0xf8, 0x00, 0x00, 0x00, 0x0e, 0x49, 0x44, 0x41, 0x54, 0x78,
    0x9c, 0x63, 0x60, 0xf8, 0xcf, 0xd0, 0xe0, 0x00, 0x00, 0x05, 0x42, 0x01, 0xc0, 0x70, 0x36,
    0x36, 0xd6, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};
constexpr std::array<std::uint8_t, 178> truncated_seed{
    0x01, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19, 0xd8, 0xe5,
    0x16, 0x64, 0x48, 0x0c, 0x50, 0xb9, 0x9b, 0x1d, 0x9c, 0x14, 0x1d, 0xc9, 0xcc, 0x23, 0x49,
    0x33, 0x9d, 0x06, 0x3c, 0x1b, 0xe0, 0x80, 0x9a, 0xfd, 0xc6, 0xae, 0x6d, 0xf4, 0xf1, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x57, 0xdd, 0x52, 0xf8, 0x00, 0x00, 0x00, 0x0e, 0x49, 0x44, 0x41, 0x54, 0x78,
    0x9c, 0x63, 0x60, 0xf8, 0xcf, 0xd0, 0xe0, 0x00, 0x00, 0x05, 0x42, 0x01, 0xc0, 0x70, 0x36,
    0x36, 0xd6, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60,
};
constexpr std::array<std::uint8_t, 179> moved_seed{
    0x01, 0x00, 0x00, 0x00, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0xfb, 0x9d, 0x5a,
    0x01, 0xf5, 0xf2, 0xc2, 0xd8, 0x12, 0x5c, 0xe8, 0x1e, 0x0b, 0xb7, 0xa4, 0x2d, 0x59, 0x0e,
    0xb4, 0x78, 0x01, 0x1b, 0xa2, 0x50, 0xe3, 0x24, 0x6d, 0xd9, 0x32, 0x0e, 0x10, 0x4b, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x57, 0xdd, 0x52, 0xf8, 0x00, 0x00, 0x00, 0x0e, 0x49, 0x44, 0x41, 0x54, 0x78,
    0x9c, 0x63, 0x60, 0xf8, 0xcf, 0xd0, 0xe0, 0x00, 0x00, 0x05, 0x42, 0x01, 0xc0, 0x70, 0x36,
    0x36, 0xd6, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};
struct Envelope {
    std::array<std::span<const std::uint8_t>, 3> payloads{};
    std::array<std::span<const std::uint8_t>, 3> digests{};
    unsigned plan{};
    unsigned rotation{};
    unsigned policy{};
    bool moved = false;
};
std::optional<Envelope> admit(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < header_size || bytes.front() != 1 || bytes.subspan(1, 1).front() > 7 ||
        bytes.subspan(2, 1).front() > 3 || bytes.subspan(3, 1).front() > 5 ||
        bytes.subspan(10, 1).front() > 1 || bytes.subspan(11, 1).front() != 0) {
        return std::nullopt;
    }
    Envelope envelope{
        .plan = bytes.subspan(1, 1).front(),
        .rotation = bytes.subspan(2, 1).front(),
        .policy = bytes.subspan(3, 1).front(),
        .moved = bytes.subspan(10, 1).front() != 0,
    };
    auto remaining = bytes.subspan(header_size);
    constexpr std::array<std::size_t, 3> limits{4096, 1024, 1024};
    for (std::size_t role = 0; role < limits.size(); ++role) {
        const auto field = bytes.subspan(4 + (2 * role), 2);
        const auto size = std::size_t{field.front()} + (std::size_t{field.back()} << 8U);
        if (size > limits.at(role) || size > remaining.size()) {
            return std::nullopt;
        }
        envelope.payloads.at(role) = remaining.first(size);
        envelope.digests.at(role) = bytes.subspan(12 + (32 * role), 32);
        if (role != 0 && size == 0 &&
            std::ranges::any_of(envelope.digests.at(role), [](auto b) { return b != 0; })) {
            return std::nullopt;
        }
        remaining = remaining.subspan(size);
    }
    if (!remaining.empty() || ((envelope.plan == 7) != !envelope.payloads.back().empty())) {
        return std::nullopt;
    }
    return envelope;
}
methods::Plan selected_plan(const Envelope& envelope) {
    methods::Plan plan;
    plan.protection = !envelope.payloads.at(1).empty();
    const auto rotation = image::QuarterTurn::from_degrees(envelope.rotation * 90);
    if (!rotation.has_value()) {
        docenhance::fuzz::fail("admitted render rotation remains a quarter turn");
    }
    plan.rotation = *rotation;
    constexpr std::array alpha{
        image::AlphaPolicy::white,
        image::AlphaPolicy::black,
        image::AlphaPolicy::reject,
    };
    plan.alpha = alpha.at(envelope.policy / 2);
    plan.profile =
        envelope.policy % 2 == 0 ? image::ProfilePolicy::embedded : image::ProfilePolicy::srgb;
    plan.output = methods::ContinuousOutput{.mode = image::ToneMode::gray};
    switch (envelope.plan) {
    case 0:
        plan.output = methods::BinaryOutput{.method = methods::FixedThreshold::create(0.5).value()};
        break;
    case 2:
        plan.output = methods::ContinuousOutput{};
        break;
    case 3:
        plan.photometric.contrast = methods::Gamma::create({.gamma = 0.8}).value();
        break;
    case 4:
        plan.output = methods::BinaryOutput{
            .method = methods::Sauvola::create({.window = 3, .k = 0.2, .r = 0.5}).value(),
        };
        break;
    case 5:
        plan.photometric.denoising =
            methods::Nlm::create({.h = 3, .patch = 3, .search = 7, .blend = 0.5}).value();
        break;
    case 6:
        plan.photometric.denoising = methods::Tvl1::create({.iterations = 10}).value();
        break;
    case 7:
        plan.photometric.restoration =
            methods::Wiener::create({.psf = methods::Kernel{}, .k = 0.01, .blend = 0.5}).value();
        break;
    default:
        break;
    }
    return plan;
}
core::ContentIdentity identity(std::span<const std::uint8_t> digest, std::size_t size) {
    constexpr std::string_view hex = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : digest) {
        result.push_back(hex.at(byte >> 4U));
        result.push_back(hex.at(byte & 15U));
    }
    return {.sha256 = std::move(result), .bytes = size};
}
core::Buffer owned(std::span<const std::uint8_t> bytes, core::Budget& budget) {
    auto result = budget.allocate(bytes.size());
    require(result.has_value(), "finite render input allocation");
    std::ranges::copy(std::as_bytes(bytes), result->bytes().begin());
    return std::move(*result);
}
pipeline::Input input_for(const Envelope& envelope, core::Budget& budget) {
    pipeline::Input input;
    input.encoded().source = owned(envelope.payloads.front(), budget);
    input.encoded().source_identity =
        identity(envelope.digests.front(), input.encoded().source.size());
    if (!envelope.payloads.at(1).empty()) {
        input.encoded().mask = owned(envelope.payloads.at(1), budget);
        input.encoded().mask_identity =
            identity(envelope.digests.at(1), input.encoded().mask.size());
    }
    if (!envelope.payloads.back().empty()) {
        input.encoded().psf = owned(envelope.payloads.back(), budget);
        input.encoded().psf_identity =
            identity(envelope.digests.back(), input.encoded().psf.size());
    }
    return input;
}
bool pending(core::Checkpoint /*at*/) noexcept {
    return true;
}
void healthy(const pipeline::Rendering& result, core::Budget& budget) {
    namespace codec = docenhance::codec;
    require(result.output.decoded_and_compared && result.output.shape.depth.bits() == 1 &&
                result.output.shape.width == 2 && result.output.shape.height == 2 &&
                !result.output.profile_embedded && !result.output.resolution,
            "healthy render physically encoded and compared PNG1");
    require(result.stages.binarization.has_value(), "healthy actual classification facts");
    if (!result.stages.binarization.has_value()) {
        docenhance::fuzz::fail("healthy actual classification facts");
    }
    const auto* const facts = std::get_if<methods::FixedFacts>(&result.stages.binarization.value());
    require(facts != nullptr && facts->black_samples == 2 && facts->white_samples == 2,
            "healthy actual fixed counts match independent constants");
    const std::array<std::uint8_t, 4> expected{0, 1, 1, 0};
    const auto pixels = image::PlaneView<const std::uint8_t>::create(
                            expected, {.width = 2, .height = 2, .stride = 2})
                            .value();
    require(codec::compare_png(result.result.bytes(),
                               {
                                   .shape = result.output.shape,
                                   .pixels = pixels,
                                   .profile = {},
                                   .resolution = std::nullopt,
                               },
                               budget)
                .has_value(),
            "healthy native decode agrees with independent logical pixels and zero physical tails");
    ++harness_counts().output_comparisons;
    ++harness_counts().healthy_completions;
}
void execute(const Envelope& envelope, std::span<const std::uint8_t> original,
             core::Budget& budget) {
    namespace exec = docenhance::exec;
    const auto plan = selected_plan(envelope);
    auto input = input_for(envelope, budget);
    std::optional<pipeline::Input> other;
    if (envelope.moved) {
        other.emplace(std::move(input));
    }
    const exec::Scheduler scheduler{exec::Concurrency::resolve(2, 2, 0, 0).value(),
                                    envelope.moved ? core::Cancellation{{}, pending}
                                                   : core::Cancellation{}};
    ++harness_counts().render_calls;
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    const auto result = pipeline::render(std::move(input), plan, budget, scheduler);
    if (envelope.moved) {
        require(!result && result.error().error.code == core::ErrorCode::argument && other->ready(),
                "all consumed Input values refuse before pending stop");
    }
    if (!result) {
        require(methods::validate(result.error().completed),
                "failure retains only valid completed facts");
    }
    if (std::ranges::equal(original, healthy_seed)) {
        require(result.has_value(), "healthy tracked render MUST complete under exact256KiB");
        healthy(*result, budget);
    } else if (std::ranges::equal(original, truncated_seed)) {
        require(!result && result.error().error.code == core::ErrorCode::input &&
                    !result.error().failed_stage && !result.error().completed.binarization,
                "truncated tracked source reaches real codec refusal");
        ++harness_counts().input_refusals;
    } else if (std::ranges::equal(original, moved_seed)) {
        if (!other.has_value()) {
            docenhance::fuzz::fail("actual moved input precedes pending same-token stop");
        }
        require(!result && result.error().error.code == core::ErrorCode::argument && other->ready(),
                "actual moved input precedes pending same-token stop");
        ++harness_counts().moved_refusals;
    }
    if (result) {
        require(
            result->result.ready() && result->output.decoded_and_compared &&
                result->output.shape == result->interpretation.output &&
                image::validate(result->interpretation) && methods::validate(result->stages),
            "every returned rendering contains actual verified output and valid completed facts");
    }
}
} // namespace
namespace docenhance::fuzz {
RenderHarnessCounts render_harness_counts() noexcept {
    return harness_counts();
}
} // namespace docenhance::fuzz
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    ++harness_counts().entry_calls;
    const std::span bytes{data, size};
    const auto envelope = admit(bytes);
    if (!envelope) {
        return 0;
    }
    const std::vector<std::uint8_t> before(bytes.begin(), bytes.end());
    constexpr std::size_t budget_bytes = std::size_t{256} * 1024;
    core::Budget budget{budget_bytes};
    execute(*envelope, bytes, budget);
    require(std::ranges::equal(bytes, before),
            "render input envelope and payload beforeimages unchanged");
    require(budget.used() == 0,
            "all actual render/input/other/output owners refunded after joined return");
    ++harness_counts().refunded_cases;
    return 0;
}
