# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
add_executable(de_tests unit/value_contracts.cpp unit/execution_states.cpp unit/filesystem_identity.cpp unit/reference.cpp unit/ownership.cpp unit/input_admission.cpp unit/application.cpp unit/io.cpp unit/transport.cpp
  unit/command_delivery.cpp unit/execution_boundaries.cpp unit/utf8.cpp unit/png_decode.cpp unit/tiff_decode.cpp unit/jpeg_decode.cpp unit/jpeg_samples.cpp unit/binarization.cpp unit/cancellation.cpp unit/cancellation_io.cpp unit/continuous.cpp unit/continuous_io.cpp unit/denoising.cpp unit/denoising_source.cpp unit/morphology.cpp unit/tvl1.cpp unit/contrast.cpp unit/illumination.cpp unit/illumination_rows.cpp unit/surface_solver.cpp unit/record.cpp unit/verify.cpp unit/instant.cpp unit/bundle_access.cpp unit/bundle_publication.cpp unit/bundle_ownership.cpp)
target_include_directories(de_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support" "${PROJECT_SOURCE_DIR}/src/io" "${PROJECT_SOURCE_DIR}/src/methods" "${PROJECT_SOURCE_DIR}/src/host")
target_link_libraries(de_tests PRIVATE de_app de_bundle de_core de_contract de_exec de_image de_methods de_io de_cli de_host de_color de_denoise Catch2::Catch2WithMain)
de_apply_options(de_tests)
# The dependency-free reference runner used by tools/check_reference_suite.py, built, linted and run here too.
add_executable(de_reference_tests support/standalone_main.cpp)
target_include_directories(de_reference_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(de_reference_tests PRIVATE de_app de_core de_contract de_exec de_image de_methods)
de_apply_options(de_reference_tests)
add_test(NAME reference-suite COMMAND de_reference_tests)
# Every fuzz harness also runs in every build, on every compiler and under the sanitize preset,
# over its seed corpus and recorded regressions; no fuzzing engine is involved.
function(de_fuzz_replay name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "SOURCE" "LINKS")
  add_executable(de_fuzz_replay_${name} "${arg_SOURCE}"
    "${PROJECT_SOURCE_DIR}/fuzz/support/replay_main.cpp")
  target_include_directories(de_fuzz_replay_${name} PRIVATE "${PROJECT_SOURCE_DIR}/fuzz"
    "${CMAKE_CURRENT_SOURCE_DIR}/support")
  target_link_libraries(de_fuzz_replay_${name} PRIVATE ${arg_LINKS})
  de_apply_options(de_fuzz_replay_${name})
  add_test(NAME fuzz-replay-${name} COMMAND de_fuzz_replay_${name}
    "${PROJECT_SOURCE_DIR}/fuzz/corpus/${name}" "${PROJECT_SOURCE_DIR}/fuzz/regressions/${name}")
endfunction()
include(FuzzTargets)
de_register_fuzz_targets(REPLAY)
include(Catch)
catch_discover_tests(de_tests DISCOVERY_MODE PRE_TEST SKIP_IS_FAILURE EXTRA_ARGS --reporter xml)
add_test(NAME cli-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_cli.py" "$<TARGET_FILE:docenhance>")
add_test(NAME png-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_png.py" "$<TARGET_FILE:docenhance>")
add_test(NAME path-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_paths.py" "$<TARGET_FILE:docenhance>")
add_test(NAME sauvola-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_sauvola.py" "$<TARGET_FILE:docenhance>")
add_test(NAME tvl1-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_tvl1.py" "$<TARGET_FILE:docenhance>")
add_test(NAME morphology-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_morphology.py" "$<TARGET_FILE:docenhance>")
add_test(NAME illumination-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_illumination.py" "$<TARGET_FILE:docenhance>")
add_test(NAME continuous-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_continuous.py" "$<TARGET_FILE:docenhance>")
add_test(NAME interpretation-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_interpretation.py" "$<TARGET_FILE:docenhance>")
set_tests_properties(interpretation-contract PROPERTIES TIMEOUT 90)
add_test(NAME bundle-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_bundles.py" "$<TARGET_FILE:docenhance>")
add_test(NAME tiff-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_tiff.py" "$<TARGET_FILE:docenhance>")
set_tests_properties(tiff-contract PROPERTIES TIMEOUT 300)
add_test(NAME jpeg-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_jpeg.py" "$<TARGET_FILE:docenhance>")
add_test(NAME jpeg-pipeline-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_jpeg_pipeline.py" "$<TARGET_FILE:docenhance>")
add_test(NAME generated-contract COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/generate_spec.py" --check)
# The compiler-driven architecture rules need GNU-style options (-H, -fsyntax-only) and a pinned
# clang-query, which an MSVC compilation database cannot provide; there only the source rules run.
if(MSVC)
  add_test(NAME architecture COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_architecture.py")
else()
  # One compiler process per source and per public header, so the worker bound is the same one every
  # other external process in this project honors.
  add_test(NAME architecture COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_architecture.py" --build "${PROJECT_BINARY_DIR}" --jobs "${DE_BUILD_JOBS}")
  # It runs that many compiler processes at once, so CTest must count them and not schedule the
  # rest of the suite on top of them.
  set_tests_properties(architecture PROPERTIES PROCESSORS "${DE_BUILD_JOBS}")
endif()
add_test(NAME project-structure COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_project.py")
add_test(NAME tooling COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/run_tooling_tests.py")
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
set_tests_properties(cli-contract png-contract path-contract sauvola-contract continuous-contract illumination-contract morphology-contract tvl1-contract PROPERTIES TIMEOUT 90)

# A test composition with a handshake, using the exact production interrupt bridge.
add_executable(de_interrupt_process support/interrupt_process.cpp "${PROJECT_SOURCE_DIR}/src/entry/signals.cpp")
target_include_directories(de_interrupt_process PRIVATE "${PROJECT_SOURCE_DIR}/src/entry")
target_link_libraries(de_interrupt_process PRIVATE de_cli de_host de_core)
de_apply_options(de_interrupt_process)
add_test(NAME interrupt-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_interrupts.py" "$<TARGET_FILE:de_interrupt_process>")
set_tests_properties(interrupt-contract PROPERTIES TIMEOUT 60)

# Developer-only allocation observation; no benchmark result is manufactured by an API estimate.
add_executable(de_jpeg_resources support/jpeg_resources.cpp)
target_include_directories(de_jpeg_resources PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(de_jpeg_resources PRIVATE de_io de_image de_core)
de_apply_options(de_jpeg_resources)

add_test(NAME composition-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_composition.py" "$<TARGET_FILE:docenhance>")
# Composition runs native stages and independent references; preserve its deadline without
# competing with another heavy test process. Internal scheduler concurrency remains exercised.
set_tests_properties(composition-contract PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)

add_test(NAME denoising-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_denoising.py" "$<TARGET_FILE:docenhance>")
set_tests_properties(denoising-contract PROPERTIES TIMEOUT 90)

add_executable(de_denoising_resources support/denoising_resources.cpp support/allocation_observer.cpp)
target_link_libraries(de_denoising_resources PRIVATE de_denoise de_methods de_image de_core opencv_core)
target_compile_definitions(de_denoising_resources PRIVATE DE_ALLOCATION_SANITIZER_OBSERVATION=$<BOOL:${DE_ENABLE_TSAN}>)
de_apply_options(de_denoising_resources)

add_test(NAME denoising-native-resources COMMAND de_denoising_resources)
set_tests_properties(denoising-native-resources PROPERTIES TIMEOUT 60)

add_test(NAME delivery-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_delivery.py" "$<TARGET_FILE:docenhance>")
set_tests_properties(delivery-contract PROPERTIES TIMEOUT 60)

add_executable(de_cli_allocation support/cli_allocation.cpp support/allocation_observer.cpp)
target_link_libraries(de_cli_allocation PRIVATE de_cli de_app de_contract de_core)
target_include_directories(de_cli_allocation PRIVATE "${PROJECT_SOURCE_DIR}/src/cli")
target_compile_definitions(de_cli_allocation PRIVATE DE_ALLOCATION_SANITIZER_OBSERVATION=$<BOOL:${DE_ENABLE_TSAN}>)
de_apply_options(de_cli_allocation)
add_test(NAME cli-allocation-failure COMMAND de_cli_allocation)
set_tests_properties(cli-allocation-failure PROPERTIES TIMEOUT 60)

add_executable(de_resource_limits support/resource_limits.cpp support/allocation_observer.cpp)
target_link_libraries(de_resource_limits PRIVATE de_bundle de_methods de_image de_core)
target_compile_definitions(de_resource_limits PRIVATE DE_ALLOCATION_SANITIZER_OBSERVATION=$<BOOL:${DE_ENABLE_TSAN}>)
de_apply_options(de_resource_limits)
add_test(NAME resource-limits COMMAND de_resource_limits)
set_tests_properties(resource-limits PROPERTIES TIMEOUT 60)

# The same project options as production must detect actual faults, not only link runtimes.
add_executable(de_sanitizer_probe support/sanitizer_probe.cpp)
de_apply_options(de_sanitizer_probe)
if(DE_ENABLE_ASAN OR DE_ENABLE_UBSAN OR DE_ENABLE_TSAN)
  add_test(NAME sanitizer-detection COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_sanitizers.py"
    --binary "$<TARGET_FILE:de_sanitizer_probe>" --build "${PROJECT_BINARY_DIR}")
  set_tests_properties(sanitizer-detection PROPERTIES TIMEOUT 120)
endif()

add_test(NAME contrast-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cli/test_contrast.py" "$<TARGET_FILE:docenhance>")
set_tests_properties(contrast-contract PROPERTIES TIMEOUT 90)

get_property(de_static_tests DIRECTORY PROPERTY TESTS)
string(JSON de_native_inventory SET "{}" "static_tests" "[]")
de_json_array(de_native_inventory "static_tests" "${de_static_tests}")
string(JSON de_native_inventory SET "${de_native_inventory}" "unit_binary" "\"$<TARGET_FILE:de_tests>\"")
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/native-tests.json" CONTENT "${de_native_inventory}\n")
