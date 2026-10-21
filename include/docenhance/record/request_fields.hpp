// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/record/identity_fields.hpp"
#include "docenhance/record/method_request_fields.hpp"
#include "docenhance/record/request.hpp"

#include <array>
#include <string_view>
#include <tuple>
#include <utility>
namespace docenhance {
template <> struct core::WireTag<record::PreserveOutputRequest> {
    static constexpr std::string_view key = "mode";
    static constexpr std::string_view value = "preserve";
};
template <> struct core::Fields<record::PreserveOutputRequest> {
    using Value = record::PreserveOutputRequest;
    static constexpr auto list = std::tuple{
        core::field("depth", &Value::depth),
        core::field("alpha", &Value::alpha),
        core::field("profile_policy", &Value::profile_policy),
    };
};
template <> struct core::WireTag<record::GrayOutputRequest> {
    static constexpr std::string_view key = "mode";
    static constexpr std::string_view value = "gray";
};
template <> struct core::Fields<record::GrayOutputRequest> {
    using Value = record::GrayOutputRequest;
    static constexpr auto list = std::tuple{
        core::field("depth", &Value::depth),
        core::field("alpha", &Value::alpha),
        core::field("profile_policy", &Value::profile_policy),
    };
};
template <> struct core::WireTag<record::BinaryOutputRequest> {
    static constexpr std::string_view key = "mode";
    static constexpr std::string_view value = "bw";
};
template <> struct core::Fields<record::BinaryOutputRequest> {
    using Value = record::BinaryOutputRequest;
    static constexpr auto list = std::tuple{
        core::field("depth", &Value::depth),
        core::field("alpha", &Value::alpha),
        core::field("profile_policy", &Value::profile_policy),
    };
};
template <> struct core::Fields<record::RequestedProtection> {
    static constexpr auto list =
        std::tuple{core::field("supplied", &record::RequestedProtection::supplied)};
};
template <> struct core::Fields<record::Request> {
    using Value = record::Request;
    static constexpr auto list = std::tuple{
        core::field("output", &Value::output),
        core::field("rotation", &Value::rotation),
        core::field("protection", &Value::protection),
        core::field("illumination", &Value::illumination),
        core::field("denoising", &Value::denoising),
        core::field("restoration", &Value::restoration),
        core::field("contrast", &Value::contrast),
        core::field("sharpening", &Value::sharpening),
        core::field("binarization", &Value::binarization),
    };
};
} // namespace docenhance
