// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
namespace docenhance::io {
inline constexpr unsigned jpeg_marker_prefix = 0xff;
inline constexpr unsigned jpeg_soi = 0xd8;
inline constexpr unsigned jpeg_eoi = 0xd9;
inline constexpr unsigned jpeg_sof_baseline = 0xc0;
inline constexpr unsigned jpeg_sof_progressive = 0xc2;
inline constexpr unsigned jpeg_dht = 0xc4;
inline constexpr unsigned jpeg_dqt = 0xdb;
inline constexpr unsigned jpeg_dri = 0xdd;
inline constexpr unsigned jpeg_sos = 0xda;
inline constexpr unsigned jpeg_restart_first = 0xd0;
inline constexpr unsigned jpeg_restart_last = 0xd7;
inline constexpr unsigned jpeg_app_first = 0xe0;
inline constexpr unsigned jpeg_app_exif = 0xe1;
inline constexpr unsigned jpeg_app_icc = 0xe2;
inline constexpr unsigned jpeg_app_jumbf = 0xeb;
inline constexpr unsigned jpeg_app_adobe = 0xee;
inline constexpr unsigned jpeg_app_last = 0xef;
inline constexpr unsigned jpeg_comment = 0xfe;
inline constexpr unsigned jpeg_sampling_shift = 4;
inline constexpr unsigned jpeg_sampling_mask = 15;
} // namespace docenhance::io
