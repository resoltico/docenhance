// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace docenhance::color {
struct InterpretationState;
struct InterpretationReaderState;
struct ReadControl {
    // Upper bound: native reader preparation may select a smaller healthy pool.
    unsigned workers = 1;
    core::Cancellation cancellation;
};
// A uniquely leased reader borrows one live run interpreter. Its scratch and transform are private.
class InterpretationReader {
  public:
    InterpretationReader(const InterpretationReader&) = delete;
    InterpretationReader& operator=(const InterpretationReader&) = delete;
    InterpretationReader(InterpretationReader&& other) noexcept;
    InterpretationReader& operator=(InterpretationReader&& other) noexcept;
    ~InterpretationReader();
    [[nodiscard]] bool ready() const noexcept;
    // May hold this reader's interpreted output: composition consumes each linear sample before
    // replacing that same position, so no additional per-reader output block is allocated.
    // Read output may use a prefix of this span; shifted overlap is refused before writing.
    [[nodiscard]] std::span<double> scratch() & noexcept;
    [[nodiscard]] std::span<double> scratch() && = delete;
    [[nodiscard]] core::Result<image::BlockObservation>
    read_gray(std::uint32_t row, std::uint32_t first, std::span<double> output);
    [[nodiscard]] core::Result<image::BlockObservation>
    read_rgb(std::uint32_t row, std::uint32_t first, std::span<double> output);

  private:
    friend class Interpretation;
    explicit InterpretationReader(InterpretationReaderState& state) noexcept;
    void release() noexcept;
    InterpretationReaderState* state_ = nullptr;
};
// One context-local interpreter borrowing a live decoded raster. Readers end before this owner.
class Interpretation {
  public:
    [[nodiscard]] static core::Result<std::unique_ptr<Interpretation>>
    create(const image::Raster& source, image::InterpretationPolicy policy, image::PngShape output,
           core::Budget& budget, const ReadControl& control = {});
    static core::Result<std::unique_ptr<Interpretation>> create(const image::Raster&&,
                                                                image::InterpretationPolicy,
                                                                image::PngShape, core::Budget&,
                                                                const ReadControl& = {}) = delete;
    Interpretation(const Interpretation&) = delete;
    Interpretation& operator=(const Interpretation&) = delete;
    Interpretation(Interpretation&&) = delete;
    Interpretation& operator=(Interpretation&&) = delete;
    ~Interpretation();
    [[nodiscard]] const image::InterpretationReport& descriptor() const& noexcept;
    [[nodiscard]] const image::InterpretationReport& descriptor() const&& = delete;
    [[nodiscard]] image::InterpretationCategory category() const noexcept;
    [[nodiscard]] unsigned workers() const noexcept;
    [[nodiscard]] core::Result<InterpretationReader> reader() &;
    [[nodiscard]] core::Result<InterpretationReader> reader() && = delete;

  private:
    explicit Interpretation(std::unique_ptr<InterpretationState> state);
    std::unique_ptr<InterpretationState> state_;
};
} // namespace docenhance::color
