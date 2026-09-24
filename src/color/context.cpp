// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "context.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <lcms2.h>
#include <lcms2_plugin.h>
#include <string>
#include <utility>

namespace docenhance::color {
namespace {
ColorMemory& memory(cmsContext context) noexcept {
    return *static_cast<ColorMemory*>(cmsGetContextUserData(context));
}
void* allocate_color(cmsContext context, cmsUInt32Number size) noexcept {
    auto& state = memory(context);
    try {
        for (auto& block : state.blocks) {
            if (!block.empty()) {
                continue;
            }
            auto allocated = state.budget.get().allocate(size);
            if (!allocated) {
                break;
            }
            block = std::move(*allocated);
            return block.bytes().data();
        }
    } catch (...) {
        // Native callbacks return null; no exception escapes into Little CMS.
        state.exhausted = true;
    }
    state.exhausted = true;
    return nullptr;
}
// Little CMS fixes the callback signature; the pointee is never modified.
// NOLINTNEXTLINE(misc-const-correctness)
void free_color(cmsContext context, void* const pointer) noexcept {
    if (pointer == nullptr) {
        return;
    }
    for (auto& block : memory(context).blocks) {
        if (block.bytes().data() == pointer) {
            block = core::Buffer{};
            return;
        }
    }
}
// The native realloc callback requires a mutable pointer argument.
// NOLINTNEXTLINE(misc-const-correctness)
void* resize_color(cmsContext context, void* const pointer, cmsUInt32Number size) noexcept {
    if (pointer == nullptr) {
        return allocate_color(context, size);
    }
    if (size == 0) {
        free_color(context, pointer);
        return nullptr;
    }
    for (auto& block : memory(context).blocks) {
        if (block.bytes().data() != pointer) {
            continue;
        }
        auto* const replacement = allocate_color(context, size);
        if (replacement != nullptr) {
            std::memcpy(replacement, pointer,
                        std::min(block.size(), static_cast<std::size_t>(size)));
            block = core::Buffer{};
        }
        return replacement;
    }
    memory(context).failed = true;
    return nullptr;
}
void color_error(cmsContext context, cmsUInt32Number /*code*/, const char* /*text*/) noexcept {
    memory(context).failed = true;
}
} // namespace
Context::Context(core::Budget& budget) : memory_{.budget = budget, .blocks = {}} {
    // Base-library memory hook, not the separately licensed fastfloat/threaded extensions.
    cmsPluginMemHandler allocator{
        .base =
            {
                .Magic = cmsPluginMagicNumber,
                .ExpectedVersion = LCMS_VERSION,
                .Type = cmsPluginMemHandlerSig,
                .Next = nullptr,
            },
        .MallocPtr = allocate_color,
        .FreePtr = free_color,
        .ReallocPtr = resize_color,
        .MallocZeroPtr = nullptr,
        .CallocPtr = nullptr,
        .DupPtr = nullptr,
    };
    context_ = cmsCreateContext(&allocator, &memory_);
    if (context_ != nullptr) {
        cmsSetLogErrorHandlerTHR(context_, color_error);
    }
}
Context::~Context() {
    if (context_ != nullptr) {
        cmsDeleteContext(context_);
    }
}
core::Error Context::error(std::string message) const {
    return {
        .code = memory_.exhausted ? core::ErrorCode::resource : core::ErrorCode::input,
        .message = std::move(message),
    };
}
void ProfileCloser::operator()(void* profile) const noexcept {
    static_cast<void>(cmsCloseProfile(profile));
}
void CurveCloser::operator()(cmsToneCurve* curve) const noexcept {
    cmsFreeToneCurve(curve);
}
void TransformCloser::operator()(void* transform) const noexcept {
    cmsDeleteTransform(transform);
}
} // namespace docenhance::color
