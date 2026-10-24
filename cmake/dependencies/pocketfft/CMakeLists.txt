# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 4.4)
project(DocEnhancePocketfft LANGUAGES NONE)
if(NOT POCKETFFT_NO_MULTITHREADING OR NOT POCKETFFT_CACHE_SIZE STREQUAL "0" OR
   NOT DOCENHANCE_POCKETFFT_FALLIBLE_VECTORS)
  message(FATAL_ERROR "The reviewed pocketfft policy requires serial calls, no plan cache and fallible vectors")
endif()
if(NOT EXISTS "${DE_UPSTREAM_SOURCE}/pocketfft_hdronly.h" OR
   NOT EXISTS "${DE_UPSTREAM_SOURCE}/LICENSE.md")
  message(FATAL_ERROR "Verified pocketfft source and complete license required")
endif()
file(SHA256 "${DE_UPSTREAM_SOURCE}/pocketfft_hdronly.h" de_header_sha256)
if(NOT de_header_sha256 STREQUAL DOCENHANCE_POCKETFFT_HEADER_SHA256)
  message(FATAL_ERROR "pocketfft header differs from its reviewed resource and API source")
endif()
# Keep the verified shared source immutable; only this build-private header is installed.
file(READ "${DE_UPSTREAM_SOURCE}/pocketfft_hdronly.h" de_header)
function(de_replace_pocketfft_constructor before after)
  string(FIND "${de_header}" "${before}" first)
  if(first LESS 0)
    message(FATAL_ERROR "The reviewed pocketfft constructor is absent")
  endif()
  string(LENGTH "${before}" length)
  math(EXPR next "${first} + ${length}")
  string(SUBSTRING "${de_header}" ${next} -1 tail)
  string(FIND "${tail}" "${before}" duplicate)
  if(NOT duplicate EQUAL -1)
    message(FATAL_ERROR "The reviewed pocketfft constructor is repeated")
  endif()
  string(REPLACE "${before}" "${after}" de_header "${de_header}")
  set(de_header "${de_header}" PARENT_SCOPE)
endfunction()
de_replace_pocketfft_constructor(
  "POCKETFFT_NOINLINE explicit cfftp(size_t length_)\n      : length(length_)"
  "POCKETFFT_NOINLINE explicit cfftp(size_t length_)\n      : length(length_), fact(0)")
de_replace_pocketfft_constructor(
  "POCKETFFT_NOINLINE explicit rfftp(size_t length_)\n      : length(length_)"
  "POCKETFFT_NOINLINE explicit rfftp(size_t length_)\n      : length(length_), fact(0)")
de_replace_pocketfft_constructor(
  "arr_info(shape_t shape_, stride_t stride_)\n      : shp(std::move(shape_)), str(std::move(stride_)) {}"
  "arr_info(const shape_t &shape_, const stride_t &stride_)\n      : shp(shape_), str(stride_) {}")
set(de_owned "${CMAKE_CURRENT_BINARY_DIR}/owned-source/pocketfft_hdronly.h")
file(CONFIGURE OUTPUT "${de_owned}" CONTENT "${de_header}" @ONLY NEWLINE_STYLE LF)
file(SHA256 "${de_owned}" de_patched_sha256)
if(NOT de_patched_sha256 STREQUAL DOCENHANCE_POCKETFFT_PATCHED_HEADER_SHA256)
  message(FATAL_ERROR "pocketfft derived header differs from the reviewed output")
endif()
include(GNUInstallDirs)
install(FILES "${de_owned}" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pocketfft")
