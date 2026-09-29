# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
# The compiler contract every shared preset states (docs/build.md). A host `c++` is a moving
# reference: Apple clang implements fewer -Wextra diagnostics than the pinned LLVM clang and GCC,
# so a local run of an analysis preset could pass on code that the required CI jobs reject.
# Each preset therefore names the toolchain it is validated with, and another compiler fails
# configure here instead of silently producing a weaker build.
include_guard(GLOBAL)
set(DE_TOOLCHAIN "pinned" CACHE STRING
  "Compiler contract: pinned (deps/tools.json) or platform (the platform's release toolchain)")
set_property(CACHE DE_TOOLCHAIN PROPERTY STRINGS pinned platform)
if(NOT DE_TOOLCHAIN MATCHES "^(pinned|platform)$")
  message(FATAL_ERROR "DE_TOOLCHAIN must be pinned or platform")
endif()
# deps/tools.json pins one LLVM release for the compiler and the analysis tools together;
# tools/install_llvm.py installs clang, its sanitizer runtime and clang-tidy from those recipes.
file(READ "${PROJECT_SOURCE_DIR}/deps/tools.json" de_tools_json)
string(JSON de_llvm_pin GET "${de_tools_json}" clang_tidy version)
string(REGEX MATCH "^[0-9]+" DE_LLVM_MAJOR "${de_llvm_pin}")
# The native families docs/build.md names; the release preset builds the shipped binary with the
# platform's own toolchain, which required CI exercises on all five supported platforms.
set(de_platform_compilers GNU Clang AppleClang MSVC)
string(JOIN ", " de_platform_named ${de_platform_compilers})

function(de_require_toolchain language)
  set(id "${CMAKE_${language}_COMPILER_ID}")
  set(found "${CMAKE_${language}_COMPILER} (${id} ${CMAKE_${language}_COMPILER_VERSION})")
  if(DE_TOOLCHAIN STREQUAL "platform")
    if(NOT id IN_LIST de_platform_compilers)
      message(FATAL_ERROR "DE_TOOLCHAIN=platform accepts ${de_platform_named}; found ${found}")
    endif()
    return()
  endif()
  # Windows pins MSVC: no LLVM-clang build of the dependency superbuild is validated there, and
  # required CI builds and packages Windows with cl.exe (tools/ci_windows.ps1).
  if(WIN32 AND id STREQUAL "MSVC")
    return()
  endif()
  string(REGEX MATCH "^[0-9]+" major "${CMAKE_${language}_COMPILER_VERSION}")
  # clang-cl reports Clang while targeting the MSVC ABI, which no preset is validated with.
  set(simulated "")
  if(DEFINED CMAKE_${language}_SIMULATE_ID)
    set(simulated "${CMAKE_${language}_SIMULATE_ID}")
  endif()
  if(NOT id STREQUAL "Clang" OR NOT major STREQUAL DE_LLVM_MAJOR OR simulated STREQUAL "MSVC")
    message(FATAL_ERROR "DE_TOOLCHAIN=pinned requires LLVM clang ${DE_LLVM_MAJOR} "
      "(deps/tools.json), never the host c++; found ${found}. Set CC and CXX and configure a "
      "fresh build directory: on macOS CC=$(brew --prefix llvm)/bin/clang "
      "CXX=$(brew --prefix llvm)/bin/clang++, on Linux CC=clang-${DE_LLVM_MAJOR} "
      "CXX=clang++-${DE_LLVM_MAJOR} (docs/build.md).")
  endif()
endfunction()

if(DE_ENABLE_FUZZING AND NOT DE_TOOLCHAIN STREQUAL "pinned")
  message(FATAL_ERROR "Fuzzing requires DE_TOOLCHAIN=pinned; use the fuzz preset")
endif()
de_require_toolchain(C)
de_require_toolchain(CXX)
