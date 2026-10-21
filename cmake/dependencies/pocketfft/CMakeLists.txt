# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 4.4)
project(DocEnhancePocketfft LANGUAGES NONE)
if(NOT POCKETFFT_NO_MULTITHREADING OR NOT POCKETFFT_CACHE_SIZE STREQUAL "0")
  message(FATAL_ERROR "The reviewed pocketfft policy requires serial calls and no plan cache")
endif()
if(NOT EXISTS "${DE_UPSTREAM_SOURCE}/pocketfft_hdronly.h" OR
   NOT EXISTS "${DE_UPSTREAM_SOURCE}/LICENSE.md")
  message(FATAL_ERROR "Verified pocketfft source and complete license required")
endif()
file(SHA256 "${DE_UPSTREAM_SOURCE}/pocketfft_hdronly.h" de_header_sha256)
if(NOT de_header_sha256 STREQUAL DOCENHANCE_POCKETFFT_HEADER_SHA256)
  message(FATAL_ERROR "pocketfft header differs from its reviewed resource and API source")
endif()
include(GNUInstallDirs)
install(FILES "${DE_UPSTREAM_SOURCE}/pocketfft_hdronly.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pocketfft")
