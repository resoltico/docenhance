# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
# Fuzz targets (docs/adr/0003-fuzzing.md). The harnesses are engine-agnostic LLVMFuzzerTestOneInput
# entry points, built here for libFuzzer or AFL++; tests/CMakeLists.txt also replays their corpora
# in every normal build. Fuzz builds are strict: ASan and UBSan with every report fatal.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR MSVC)
  message(FATAL_ERROR "Fuzzing requires LLVM clang, or AFL++'s afl-clang-fast++ which wraps it; "
    "set CC and CXX when configuring the fuzz preset (docs/build.md)")
endif()
file(READ "${PROJECT_SOURCE_DIR}/deps/tools.json" de_tools_json)
string(JSON de_fuzz_llvm_major GET "${de_tools_json}" fuzzing llvm_major)
string(REGEX MATCH "^[0-9]+" de_fuzz_cxx_major "${CMAKE_CXX_COMPILER_VERSION}")
if(NOT de_fuzz_cxx_major STREQUAL de_fuzz_llvm_major)
  message(FATAL_ERROR "Fuzzing uses LLVM clang ${de_fuzz_llvm_major} (deps/tools.json); "
    "found ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_VERSION}")
endif()
if(NOT DE_ENABLE_ASAN OR NOT DE_ENABLE_UBSAN)
  message(FATAL_ERROR "Fuzz builds require DE_ENABLE_ASAN and DE_ENABLE_UBSAN; use the fuzz preset")
endif()
if(DE_FUZZ_ENGINE STREQUAL "afl")
  get_filename_component(de_fuzz_cxx_name "${CMAKE_CXX_COMPILER}" NAME)
  if(NOT de_fuzz_cxx_name MATCHES "^afl-clang-(fast|lto)\\+\\+$")
    message(FATAL_ERROR "DE_FUZZ_ENGINE=afl requires CXX=afl-clang-fast++ or afl-clang-lto++")
  endif()
endif()
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=fuzzer)
check_cxx_source_compiles([[
#include <cstddef>
#include <cstdint>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t) { return 0; }
]] DE_HAVE_FUZZER_RUNTIME)
unset(CMAKE_REQUIRED_LINK_OPTIONS)
if(NOT DE_HAVE_FUZZER_RUNTIME)
  message(FATAL_ERROR "The compiler cannot link -fsanitize=fuzzer; install LLVM's fuzzer runtime")
endif()

# Instrumentation is applied by src/CMakeLists.txt to the production library targets. There is no
# second source list, alternate implementation, or concrete I/O dependency in this build.
# One engine binary and one time-boxed CTest run (tools/run_fuzzers.py) per harness.
function(de_fuzz_target name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "SOURCE" "LINKS")
  add_executable(de_fuzz_${name} "${arg_SOURCE}")
  target_include_directories(de_fuzz_${name} PRIVATE "${PROJECT_SOURCE_DIR}/fuzz"
    "${PROJECT_SOURCE_DIR}/tests/support")
  target_link_libraries(de_fuzz_${name} PRIVATE ${arg_LINKS})
  de_apply_options(de_fuzz_${name})
  target_compile_options(de_fuzz_${name} PRIVATE -fsanitize=fuzzer)
  target_link_options(de_fuzz_${name} PRIVATE -fsanitize=fuzzer)
  add_test(NAME fuzz-${name}
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/run_fuzzers.py"
      --engine ${DE_FUZZ_ENGINE} --target ${name} --binary "$<TARGET_FILE:de_fuzz_${name}>"
      --work "${PROJECT_BINARY_DIR}/fuzz-work" --seconds ${DE_FUZZ_SECONDS})
  math(EXPR de_timeout "${DE_FUZZ_SECONDS} + 120")
  set_tests_properties(fuzz-${name} PROPERTIES TIMEOUT ${de_timeout})
endfunction()
de_fuzz_target(pages SOURCE "${PROJECT_SOURCE_DIR}/fuzz/pages.cpp" LINKS de_contract)
de_fuzz_target(decimal SOURCE "${PROJECT_SOURCE_DIR}/fuzz/decimal.cpp" LINKS de_contract)
de_fuzz_target(numeric SOURCE "${PROJECT_SOURCE_DIR}/fuzz/numeric.cpp" LINKS de_image)
de_fuzz_target(box_mean SOURCE "${PROJECT_SOURCE_DIR}/fuzz/box_mean.cpp" LINKS de_methods)
de_fuzz_target(cli SOURCE "${PROJECT_SOURCE_DIR}/fuzz/cli.cpp"
  LINKS de_cli de_contract nlohmann_json::nlohmann_json)
