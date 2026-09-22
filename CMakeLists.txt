# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 4.4)
# CMake 4 no longer adds -isysroot on macOS, so standalone tools reading the compilation
# database (clang-tidy, clangd) cannot find the SDK's C++ standard library. Name it explicitly.
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_SYSROOT)
  set(CMAKE_OSX_SYSROOT "macosx" CACHE STRING "macOS SDK used by compilers and clang-tidy" FORCE)
endif()
# The single source of the application version; tools/project_version.py reads it from here.
project(DocEnhance VERSION 0.2.0 DESCRIPTION "Local, CPU-only document image restoration" LANGUAGES C CXX)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are prohibited. Use cmake --preset dev")
endif()
# All first-party targets use headers and translation units, not CXX_MODULES file sets.
# Avoid implicit GCC module-scanner flags in compile commands consumed by Clang tooling.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_LINK_LIBRARIES_ONLY_TARGETS ON)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)
set(CMAKE_EXPORT_NO_PACKAGE_REGISTRY ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
# Every reviewed policy file this configure reads. Without this, editing one of them changes
# nothing until somebody deletes the build tree: the dependency feature policy in particular was
# read once and never looked at again.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/deps/features.json"
  "${PROJECT_SOURCE_DIR}/deps/lock.json"
  "${PROJECT_SOURCE_DIR}/deps/tools.json"
  "${PROJECT_SOURCE_DIR}/spec/architecture.json")
include(Options)
find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
include(CTest)
if(DE_SUPERBUILD)
  include(Superbuild)
  return()
endif()
include(Dependencies)
include(ProjectOptions)
include(ArchitectureChecks)
file(SHA256 "${PROJECT_SOURCE_DIR}/deps/lock.json" DE_LOCK_SHA256)
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated/docenhance")
configure_file(cmake/version.hpp.in generated/docenhance/version.hpp @ONLY)
add_subdirectory(src)
if(DE_FUZZ_ONLY)
  add_subdirectory(fuzz)
  return()
endif()
if(DE_BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(DE_ENABLE_FUZZING)
  add_subdirectory(fuzz)
endif()
add_custom_target(check-spec
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/generate_spec.py" --check
  VERBATIM)
add_custom_target(check-architecture
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_architecture.py"
          --build "${PROJECT_BINARY_DIR}"
  VERBATIM)
add_custom_target(check-project
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_project.py"
  VERBATIM)
add_custom_target(check-gates
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_gates.py"
  VERBATIM)
add_custom_target(check-format
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_format.py"
  VERBATIM)
include(Packaging)
