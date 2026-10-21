// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
namespace docenhance::fft {
using Complex = std::complex<double>;
class Spectrum;
[[nodiscard]] core::Result<Spectrum> forward(image::PlaneView<const double> input,
                                             core::Budget& budget,
                                             const exec::Scheduler& scheduler);
[[nodiscard]] core::Result<void> inverse(const Spectrum& input, image::PlaneView<double> output,
                                         core::Budget& budget, const exec::Scheduler& scheduler);
// Opaque provider capacity only; charged real/spectrum payloads are additional.
[[nodiscard]] core::Result<std::size_t> workspace_bytes(image::Extent extent,
                                                        unsigned workers) noexcept;
class Spectrum {
  public:
    Spectrum() noexcept = default;
    ~Spectrum() = default;
    Spectrum(const Spectrum&) = delete;
    Spectrum& operator=(const Spectrum&) = delete;
    Spectrum(Spectrum&& other) noexcept;
    Spectrum& operator=(Spectrum&& other) noexcept;
    [[nodiscard]] bool empty() const noexcept {
        return buffer_.empty();
    }
    [[nodiscard]] image::Extent extent() const noexcept {
        return extent_;
    }
    [[nodiscard]] image::PlaneShape shape() const noexcept {
        return shape_;
    }
    [[nodiscard]] std::size_t size_bytes() const noexcept {
        return buffer_.size();
    }
    [[nodiscard]] std::span<Complex> storage() & noexcept;
    [[nodiscard]] std::span<const Complex> storage() const& noexcept;
    [[nodiscard]] std::span<Complex> storage() && = delete;
    [[nodiscard]] std::span<const Complex> storage() const&& = delete;
    [[nodiscard]] std::span<Complex> row(std::uint32_t y) & noexcept;
    [[nodiscard]] std::span<const Complex> row(std::uint32_t y) const& noexcept;
    [[nodiscard]] std::span<Complex> row(std::uint32_t y) && = delete;
    [[nodiscard]] std::span<const Complex> row(std::uint32_t y) const&& = delete;

  private:
    template <typename Self> [[nodiscard]] static auto samples_of(Self& self) noexcept {
        using Element = std::conditional_t<std::is_const_v<Self>, const Complex, Complex>;
        const auto bytes = self.buffer_.bytes();
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        auto* const data = reinterpret_cast<Element*>(bytes.data());
        return std::span<Element>{data, bytes.size() / sizeof(Complex)};
    }
    friend core::Result<Spectrum> forward(image::PlaneView<const double> input,
                                          core::Budget& budget, const exec::Scheduler& scheduler);
    friend core::Result<void> inverse(const Spectrum& input, image::PlaneView<double> output,
                                      core::Budget& budget, const exec::Scheduler& scheduler);
    [[nodiscard]] static core::Result<Spectrum> allocate(core::Budget& budget, image::Extent extent,
                                                         const core::Cancellation& cancellation);
    Spectrum(core::Buffer buffer, image::Extent extent, image::PlaneShape shape) noexcept;
    core::Buffer buffer_;
    image::Extent extent_{};
    image::PlaneShape shape_{};
};
} // namespace docenhance::fft
