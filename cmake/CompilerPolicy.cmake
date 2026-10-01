# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
# The compiler contract every shared preset states (docs/build.md). A host `c++` is a moving
# reference: Apple clang implements fewer -Wextra diagnostics than the pinned LLVM clang and GCC,
# so a local run of an analysis preset could pass on code that the required CI jobs reject.
# Each preset therefore names the toolchain it is validated with, and another compiler fails
# configure here instead of silently producing a weaker build.
include_guard(GLOBAL)
set(DE_TOOLCHAIN "analysis" CACHE STRING
  "Compiler contract: analysis (the reviewed compiler family and LLVM major) or platform (the platform's release toolchain)")
set_property(CACHE DE_TOOLCHAIN PROPERTY STRINGS analysis platform)
if(NOT DE_TOOLCHAIN MATCHES "^(analysis|platform)$")
  message(FATAL_ERROR "DE_TOOLCHAIN must be analysis or platform")
endif()
# The LLVM major is the supported analysis contract. Installer source/binary recipes have
# their own exact identities where available; platform patch releases are not falsely pinned.
file(READ "${PROJECT_SOURCE_DIR}/deps/tools.json" de_tools_json)
string(JSON de_llvm_pin GET "${de_tools_json}" clang_tidy version)
string(REGEX MATCH "^[0-9]+" DE_LLVM_MAJOR "${de_llvm_pin}")
# The native families docs/build.md names; the release preset builds the shipped binary with the
# platform's own toolchain, which required CI exercises on all five supported platforms.

function(de_require_toolchain language)
  set(id "${CMAKE_${language}_COMPILER_ID}")
  set(found "${CMAKE_${language}_COMPILER} (${id} ${CMAKE_${language}_COMPILER_VERSION})")
  if(WIN32)
    if(NOT id STREQUAL "MSVC")
      message(FATAL_ERROR "Windows builds require the validated native MSVC compiler; found ${found}")
    endif()
    return()
  endif()
  if(DE_TOOLCHAIN STREQUAL "platform")
    if((APPLE AND NOT id STREQUAL "AppleClang") OR
       (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT id STREQUAL "GNU"))
      message(FATAL_ERROR "The release compiler is AppleClang on macOS or GNU on Linux; found ${found}")
    endif()
    return()
  endif()
  string(REGEX MATCH "^[0-9]+" major "${CMAKE_${language}_COMPILER_VERSION}")
  # clang-cl reports Clang while targeting the MSVC ABI, which no preset is validated with.
  set(simulated "")
  if(DEFINED CMAKE_${language}_SIMULATE_ID)
    set(simulated "${CMAKE_${language}_SIMULATE_ID}")
  endif()
  if(NOT id STREQUAL "Clang" OR NOT major STREQUAL DE_LLVM_MAJOR OR simulated STREQUAL "MSVC")
    message(FATAL_ERROR "DE_TOOLCHAIN=analysis requires LLVM clang ${DE_LLVM_MAJOR} "
      "(deps/tools.json), never the host c++; found ${found}. Set CC and CXX and configure a "
      "fresh build directory: on macOS CC=$(brew --prefix llvm)/bin/clang "
      "CXX=$(brew --prefix llvm)/bin/clang++, on Linux CC=clang-${DE_LLVM_MAJOR} "
      "CXX=clang++-${DE_LLVM_MAJOR} (docs/build.md).")
  endif()
endfunction()

if(DE_ENABLE_FUZZING AND NOT DE_TOOLCHAIN STREQUAL "analysis")
  message(FATAL_ERROR "Fuzzing requires DE_TOOLCHAIN=analysis; use the fuzz preset")
endif()
de_require_toolchain(C)
de_require_toolchain(CXX)
