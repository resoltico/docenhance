# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 4.4)
include(cmake/BuildPolicy.cmake)
# The single source of the application version; tools/project_version.py reads it from here.
project(DocEnhance VERSION 0.5.0 DESCRIPTION "Local, CPU-only document image restoration" LANGUAGES C CXX)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are prohibited. Use cmake --preset dev")
endif()
# All first-party targets use headers and translation units, not CXX_MODULES file sets.
# Avoid implicit GCC module-scanner flags in compile commands consumed by Clang tooling.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_LINK_LIBRARIES_ONLY_TARGETS ON)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)
set(CMAKE_EXPORT_NO_PACKAGE_REGISTRY ON)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
# Every reviewed policy file this configure reads. Without this, editing one of them changes
# nothing until somebody deletes the build tree: the dependency feature policy in particular was
# read once and never looked at again.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/deps/features.json"
  "${PROJECT_SOURCE_DIR}/deps/lock.json"
  "${PROJECT_SOURCE_DIR}/deps/tools.json"
  "${PROJECT_SOURCE_DIR}/spec/architecture.json"
  "${PROJECT_SOURCE_DIR}/fuzz/targets.json"
  "${PROJECT_SOURCE_DIR}/tools/fuzz_manifest.py")
find_package(Python3 ${DE_PYTHON_MINIMUM} REQUIRED COMPONENTS Interpreter)
include(Options)
include(CompilerPolicy)
de_validate_native_configuration()
if(DE_BUILD_TESTS)
  execute_process(COMMAND "${Python3_EXECUTABLE}" -c
    "import importlib.metadata,json,sys; p=json.load(open(sys.argv[1],encoding='utf-8')); expected=p['jsonschema']['version']; actual=importlib.metadata.version('jsonschema'); sys.exit(0 if actual==expected else 'jsonschema '+expected+' is required; found '+actual)"
    "${PROJECT_SOURCE_DIR}/deps/tools.json" RESULT_VARIABLE de_python_environment)
  if(NOT de_python_environment EQUAL 0)
    message(FATAL_ERROR "The selected Python test environment lacks pinned jsonschema. Activate the project environment and run tools/install_build_tools.py")
  endif()
endif()
include(DependencyPlan)
include(BuildIdentity)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <expected>
#include <span>
#include <stop_token>
#include <thread>
#include <charconv>
int main() {
  std::expected<int, int> value = 1;
  std::stop_source source;
  std::jthread thread;
  int n = 0;
  const char text[] = { '1' };
  auto result = std::from_chars(text, text + 1, n);
  std::span<const char> bytes{text};
  return *value + static_cast<int>(source.stop_requested()) + n + static_cast<int>(bytes.size()) +
         static_cast<int>(result.ec == std::errc{});
}" DE_NATIVE_LIBRARY_AVAILABLE)
if(NOT DE_NATIVE_LIBRARY_AVAILABLE)
  message(FATAL_ERROR "The selected compiler, standard library and deployment target lack the required C++ library")
endif()
if(DE_BUILD_TESTS OR DE_ENABLE_FUZZING)
  enable_testing()
endif()
if(DE_SUPERBUILD)
  include(Superbuild)
  de_commit_build_identity()
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
  de_commit_build_identity()
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
          --build "${PROJECT_BINARY_DIR}" --jobs "${DE_BUILD_JOBS}"
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

de_commit_build_identity()
