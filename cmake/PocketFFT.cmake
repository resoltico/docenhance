# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
include_guard(GLOBAL)
include(DependencyPaths)
# Header-only FFT provider; the selected file must be owned and match reviewed bytes.
find_path(DE_POCKETFFT_INCLUDE NAMES pocketfft_hdronly.h
  PATHS "${DE_DEPENDENCY_PREFIX}/include/pocketfft" NO_DEFAULT_PATH REQUIRED)
de_require_owned_dependency_path("${DE_POCKETFFT_INCLUDE}" "pocketfft headers")
de_require_owned_dependency_path("${DE_POCKETFFT_INCLUDE}/pocketfft_hdronly.h" "pocketfft selected header")
file(READ "${PROJECT_SOURCE_DIR}/deps/features.json" de_pocketfft_features)
string(JSON de_pocketfft_serial GET "${de_pocketfft_features}" dependencies pocketfft POCKETFFT_NO_MULTITHREADING)
string(JSON de_pocketfft_cache GET "${de_pocketfft_features}" dependencies pocketfft POCKETFFT_CACHE_SIZE)
string(JSON de_pocketfft_fallible GET "${de_pocketfft_features}" dependencies pocketfft DOCENHANCE_POCKETFFT_FALLIBLE_VECTORS)
if(NOT de_pocketfft_serial OR NOT de_pocketfft_cache STREQUAL "0" OR NOT de_pocketfft_fallible)
  message(FATAL_ERROR "pocketfft requires serial calls, no plan cache and fallible vectors")
endif()
string(JSON de_pocketfft_expected GET "${de_pocketfft_features}" dependencies pocketfft DOCENHANCE_POCKETFFT_PATCHED_HEADER_SHA256)
file(SHA256 "${DE_POCKETFFT_INCLUDE}/pocketfft_hdronly.h" de_pocketfft_actual)
if(NOT de_pocketfft_actual STREQUAL de_pocketfft_expected)
  message(FATAL_ERROR "pocketfft selected header digest differs from the reviewed provider")
endif()
add_library(DE::pocketfft INTERFACE IMPORTED GLOBAL)
set_target_properties(DE::pocketfft PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${DE_POCKETFFT_INCLUDE}"
  INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${DE_POCKETFFT_INCLUDE}"
  INTERFACE_COMPILE_DEFINITIONS "POCKETFFT_NO_MULTITHREADING;POCKETFFT_CACHE_SIZE=${de_pocketfft_cache}")
