# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
add_executable(de_tests unit/foundation.cpp)
target_include_directories(de_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(de_tests PRIVATE de_core de_contract de_exec de_image de_methods de_io Catch2::Catch2WithMain)
de_apply_options(de_tests)
# The dependency-free reference runner used by tools/check_foundation.py, built, linted and run here too.
add_executable(de_reference_tests support/standalone_main.cpp)
target_include_directories(de_reference_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(de_reference_tests PRIVATE de_core de_contract de_exec de_image de_methods de_io)
de_apply_options(de_reference_tests)
add_test(NAME reference-suite COMMAND de_reference_tests)
# Every fuzz harness also runs in every build, on every compiler and under the sanitize preset,
# over its seed corpus and recorded regressions; no fuzzing engine is involved.
function(de_fuzz_replay name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "SOURCE" "LINKS")
  add_executable(de_fuzz_replay_${name} "${arg_SOURCE}"
    "${PROJECT_SOURCE_DIR}/fuzz/support/replay_main.cpp")
  target_include_directories(de_fuzz_replay_${name} PRIVATE "${PROJECT_SOURCE_DIR}/fuzz")
  target_link_libraries(de_fuzz_replay_${name} PRIVATE ${arg_LINKS})
  de_apply_options(de_fuzz_replay_${name})
  add_test(NAME fuzz-replay-${name} COMMAND de_fuzz_replay_${name}
    "${PROJECT_SOURCE_DIR}/fuzz/corpus/${name}" "${PROJECT_SOURCE_DIR}/fuzz/regressions/${name}")
endfunction()
de_fuzz_replay(pages SOURCE "${PROJECT_SOURCE_DIR}/fuzz/pages.cpp" LINKS de_contract)
de_fuzz_replay(decimal SOURCE "${PROJECT_SOURCE_DIR}/fuzz/decimal.cpp" LINKS de_contract)
de_fuzz_replay(numeric SOURCE "${PROJECT_SOURCE_DIR}/fuzz/numeric.cpp" LINKS de_image)
de_fuzz_replay(box_mean SOURCE "${PROJECT_SOURCE_DIR}/fuzz/box_mean.cpp"
  LINKS de_core de_exec de_image de_methods)
de_fuzz_replay(cli SOURCE "${PROJECT_SOURCE_DIR}/fuzz/cli.cpp" LINKS de_cli nlohmann_json::nlohmann_json)
include(Catch)
catch_discover_tests(de_tests DISCOVERY_MODE PRE_TEST)
add_test(NAME cli-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_cli.py" "$<TARGET_FILE:docenhance>")
add_test(NAME generated-contract COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/generate_spec.py" --check)
# The compiler-driven architecture rules need GNU-style options (-M, -fsyntax-only) and a pinned
# clang-query, which an MSVC compilation database cannot provide; there only the source rules run.
if(MSVC)
  add_test(NAME architecture COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_architecture.py")
else()
  add_test(NAME architecture COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_architecture.py" --build "${PROJECT_BINARY_DIR}")
endif()
add_test(NAME project-structure COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_project.py")
add_test(NAME tooling COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tooling" -v)
if(TARGET de_native_probe)
  add_test(NAME dependency-native-link COMMAND de_native_probe)
  # stdout carries the JSON response and nothing else. OpenCV logs there by default, so the probe
  # silences every library first and this asserts that the silence held.
  set_tests_properties(dependency-native-link PROPERTIES
    FAIL_REGULAR_EXPRESSION "INFO:|WARNING:|ERROR:|Warning:")
endif()
if(DEFINED DE_SUPERBUILD_BINARY)
  add_test(NAME dependency-configuration COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/audit_build.py" --build "${DE_SUPERBUILD_BINARY}")
endif()
set_tests_properties(cli-contract PROPERTIES TIMEOUT 30)
