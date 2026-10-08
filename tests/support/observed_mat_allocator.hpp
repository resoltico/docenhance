// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "allocation_observer.hpp"

#include <cstddef>
#include <functional>
#include <opencv2/core/exception.hpp>
#include <opencv2/core/mat.hpp>
namespace docenhance::tests {
class ObservedMatAllocator final : public cv::MatAllocator {
  public:
    explicit ObservedMatAllocator(cv::MatAllocator& upstream, bool refuse_payload = false)
        : upstream_(upstream), refuse_payload_(refuse_payload) {}
    // Fixed public native allocator ABI, including shape, storage and access flags.
    // NOLINTNEXTLINE(google-readability-function-size)
    cv::UMatData* allocate(int dimensions, const int* sizes, int type, void* data,
                           std::size_t* step, cv::AccessFlag flags,
                           cv::UMatUsageFlags usage) const override {
        if (data == nullptr && (refuse_payload_ || allocation_observer::refuse_allocation())) {
            CV_Error(cv::Error::StsNoMem, "Observed native Mat allocation refusal");
        }
        auto* const result =
            upstream_.get().allocate(dimensions, sizes, type, data, step, flags, usage);
        if (!DE_ALLOCATION_SANITIZER_OBSERVATION && result != nullptr && data == nullptr &&
            allocation_observer::observing.load()) {
            allocation_observer::acquire_bytes(result->size);
            result->currAllocator = this;
        }
        return result;
    }
    bool allocate(cv::UMatData* data, cv::AccessFlag flags,
                  cv::UMatUsageFlags usage) const override {
        return upstream_.get().allocate(data, flags, usage);
    }
    void deallocate(cv::UMatData* data) const override {
        if (!DE_ALLOCATION_SANITIZER_OBSERVATION && data != nullptr) {
            allocation_observer::live.fetch_sub(data->size);
            data->currAllocator = &upstream_.get();
        }
        upstream_.get().deallocate(data);
    }

  private:
    std::reference_wrapper<cv::MatAllocator> upstream_;
    bool refuse_payload_;
};
} // namespace docenhance::tests
