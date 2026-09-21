# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
include_guard(GLOBAL)
add_library(de_project_options INTERFACE)
add_library(DocEnhance::options ALIAS de_project_options)
target_compile_features(de_project_options INTERFACE cxx_std_23)
if(MSVC)
  target_compile_options(de_project_options INTERFACE /W4 /permissive- /Zc:__cplusplus /utf-8 /EHsc /fp:strict)
  if(DE_WARNINGS_AS_ERRORS)
    target_compile_options(de_project_options INTERFACE /WX)
  endif()
else()
  # Keep DE_STRICT_WARNINGS in sync with tools/check_foundation.py (checked by check_project.py).
  set(DE_STRICT_WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wformat=2
    -Wold-style-cast -Wcast-align -Wcast-qual -Wnon-virtual-dtor -Woverloaded-virtual -Wdouble-promotion
    -Wimplicit-fallthrough -Wmissing-declarations -Wundef -Wextra-semi)
  target_compile_options(de_project_options INTERFACE ${DE_STRICT_WARNINGS} -fno-fast-math -ffp-contract=off)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(de_project_options INTERFACE -Wduplicated-cond -Wduplicated-branches -Wlogical-op)
  endif()
  if(DE_WARNINGS_AS_ERRORS)
    target_compile_options(de_project_options INTERFACE -Werror)
  endif()
endif()
# ThreadSanitizer shares no runtime with AddressSanitizer, so it is a preset of its own.
if(DE_ENABLE_TSAN AND DE_ENABLE_ASAN)
  message(FATAL_ERROR "ThreadSanitizer and AddressSanitizer cannot be combined; use one preset")
endif()
if(DE_ENABLE_ASAN OR DE_ENABLE_UBSAN OR DE_ENABLE_TSAN)
  if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "The sanitizer preset requires Clang or GCC on Linux/macOS")
  endif()
  set(de_sanitizers)
  if(DE_ENABLE_TSAN)
    list(APPEND de_sanitizers thread)
  endif()
  if(DE_ENABLE_ASAN)
    list(APPEND de_sanitizers address)
  endif()
  if(DE_ENABLE_UBSAN)
    # Beyond -fsanitize=undefined: floating division by zero, out-of-bounds indexing of local
    # arrays, and (Clang) implicit conversions that change a value's sign or truncate it.
    list(APPEND de_sanitizers undefined float-divide-by-zero)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      list(APPEND de_sanitizers local-bounds implicit-conversion)
    else()
      list(APPEND de_sanitizers bounds-strict)
    endif()
  endif()
  list(JOIN de_sanitizers "," de_sanitizers)
  # Strict: every report aborts (UBSan otherwise prints and continues, so tests would pass), and
  # the standard library checks its own preconditions (bounds, iterator validity).
  target_compile_options(de_project_options INTERFACE "-fsanitize=${de_sanitizers}"
    -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_compile_definitions(de_project_options INTERFACE _GLIBCXX_ASSERTIONS
    _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE)
  target_link_options(de_project_options INTERFACE "-fsanitize=${de_sanitizers}"
    -fno-sanitize-recover=all)
endif()
if(DE_ENABLE_IPO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT DE_IPO_SUPPORTED OUTPUT DE_IPO_ERROR LANGUAGES C CXX)
  if(NOT DE_IPO_SUPPORTED)
    message(FATAL_ERROR "IPO requested but unavailable: ${DE_IPO_ERROR}")
  endif()
endif()
if(DE_ENABLE_CLANG_TIDY)
  # Lint results depend on the clang-tidy release, so the major version must match the pin.
  file(READ "${PROJECT_SOURCE_DIR}/deps/tools.json" de_tools_json)
  string(JSON de_tidy_pin GET "${de_tools_json}" clang_tidy version)
  string(REGEX MATCH "^[0-9]+" de_tidy_major "${de_tidy_pin}")
  # A Homebrew formula rename can leave a configured build tree with an executable path that no
  # longer exists. Re-resolve it instead of reporting an opaque process-launch error.
  if(DE_CLANG_TIDY AND NOT EXISTS "${DE_CLANG_TIDY}")
    unset(DE_CLANG_TIDY CACHE)
  endif()
  find_program(DE_CLANG_TIDY NAMES clang-tidy-${de_tidy_major} clang-tidy
    HINTS ENV DE_CLANG_TIDY_DIR
          /opt/homebrew/opt/llvm@${de_tidy_major}/bin /usr/local/opt/llvm@${de_tidy_major}/bin
          /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin
    REQUIRED)
  execute_process(COMMAND "${DE_CLANG_TIDY}" --version OUTPUT_VARIABLE DE_CLANG_TIDY_VERSION_TEXT COMMAND_ERROR_IS_FATAL ANY)
  string(REGEX MATCH "version ([0-9]+)\\." de_tidy_found "${DE_CLANG_TIDY_VERSION_TEXT}")
  if(NOT CMAKE_MATCH_1 STREQUAL de_tidy_major)
    message(FATAL_ERROR "clang-tidy ${de_tidy_major}.x is required by deps/tools.json; "
      "found ${DE_CLANG_TIDY}: '${DE_CLANG_TIDY_VERSION_TEXT}'")
  endif()
  # No --config-file: clang-tidy must discover the nearest .clang-tidy (tests/ has documented
  # overrides). --warnings-as-errors on the command line cannot be relaxed by any directory config.
  # clang-tidy parses GCC's compile commands too, and those carry the GCC-only warnings below.
  # With -Werror an option clang does not know is a hard compiler error, so clang-tidy would fail
  # on every GCC platform. This disables that one diagnostic for clang-tidy's parse only: GCC
  # still receives and enforces the flags, and unknown options remain errors in the real build.
  set(DE_CLANG_TIDY_COMMAND
    "${DE_CLANG_TIDY}"
    "--warnings-as-errors=*"
    "--extra-arg=-Wno-unknown-warning-option"
    "--use-color")
  if(MSVC)
    # clang-tidy parses the compilation database through clang-cl, whose default exception mode
    # differs from cl.exe even when the real command has /EHsc. The CLI adapter owns one catch.
    list(APPEND DE_CLANG_TIDY_COMMAND "--extra-arg-before=/EHsc")
  endif()
endif()
function(de_apply_options target)
  target_link_libraries(${target} PRIVATE DocEnhance::options)
  if(DE_ENABLE_IPO)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  endif()
  if(DE_ENABLE_CLANG_TIDY)
    set_property(TARGET ${target} PROPERTY CXX_CLANG_TIDY "${DE_CLANG_TIDY_COMMAND}")
  endif()
endfunction()
