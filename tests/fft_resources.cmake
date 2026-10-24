# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
# Compile/link and retain the actual C/C++ resource control on every native lane.
if(APPLE)
  set(de_fft_observer_kind SHARED)
else()
  set(de_fft_observer_kind STATIC)
endif()
add_library(de_fft_allocation_observer ${de_fft_observer_kind} support/allocation_observer.cpp support/fft_allocation_trace.cpp support/fft_allocation_gate.cpp)
if(NOT WIN32)
  target_sources(de_fft_allocation_observer PRIVATE support/allocation_interpose.cpp)
endif()
target_compile_definitions(de_fft_allocation_observer PRIVATE DE_ALLOCATION_SANITIZER_OBSERVATION=0 DE_ALLOCATION_C_OBSERVATION=1)
target_link_libraries(de_fft_allocation_observer PRIVATE DE::zlib)
de_apply_options(de_fft_allocation_observer)
add_executable(de_fft_resources support/fft_resources.cpp)
if(WIN32)
  # A direct EXE object retains the real C replacement entries without whole-archive tricks.
  target_sources(de_fft_resources PRIVATE support/allocation_interpose.cpp)
  target_compile_definitions(de_fft_resources PRIVATE DE_ALLOCATION_C_OBSERVATION=1)
  target_link_options(de_fft_resources PRIVATE /VERBOSE:LIB
    "/MAP:${CMAKE_CURRENT_BINARY_DIR}/de_fft_resources.map")
endif()
target_include_directories(de_fft_resources PRIVATE "${PROJECT_SOURCE_DIR}/src/fft" "${PROJECT_SOURCE_DIR}/src/color")
if(APPLE AND DE_ENABLE_ASAN)
  # Preserve the compiler's sanitizer flags; order its own runtime before the C observer.
  execute_process(COMMAND "${CMAKE_CXX_COMPILER}" --print-runtime-dir
    OUTPUT_VARIABLE de_asan_directory OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${CMAKE_CXX_COMPILER}" --print-file-name=libclang_rt.asan_osx_dynamic.dylib
    OUTPUT_VARIABLE de_asan_selected OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  cmake_path(IS_ABSOLUTE de_asan_selected de_asan_absolute)
  if(NOT de_asan_absolute OR NOT EXISTS "${de_asan_selected}" OR IS_DIRECTORY "${de_asan_selected}")
    message(FATAL_ERROR "The configured compiler must select an existing ASan dynamic runtime")
  endif()
  file(REAL_PATH "${de_asan_directory}/libclang_rt.asan_osx_dynamic.dylib" de_asan_expected)
  file(REAL_PATH "${de_asan_selected}" de_asan_actual)
  if(NOT de_asan_actual STREQUAL de_asan_expected)
    message(FATAL_ERROR "ASan runtime selection differs from the configured compiler runtime directory")
  endif()
  target_link_libraries(de_fft_resources PRIVATE "${de_asan_selected}")
  file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/fft-asan-runtime.txt" CONTENT "${de_asan_selected}\n")
endif()
target_link_libraries(de_fft_resources PRIVATE de_fft de_image de_exec de_core de_color DE::lcms de_fft_allocation_observer)
de_apply_options(de_fft_resources)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(de_fft_resources PRIVATE "LINKER:--wrap=malloc" "LINKER:--wrap=free")
endif()
add_test(NAME fft-native-resources COMMAND de_fft_resources)
set_tests_properties(fft-native-resources PROPERTIES TIMEOUT 90)

# Complete R01 calls share the actual ordinary C/C++ observer and selected provider route.
add_executable(de_restoration_resources support/restoration_resources.cpp)
if(WIN32)
  target_sources(de_restoration_resources PRIVATE support/allocation_interpose.cpp)
  target_compile_definitions(de_restoration_resources PRIVATE DE_ALLOCATION_C_OBSERVATION=1)
  target_link_options(de_restoration_resources PRIVATE /VERBOSE:LIB
    "/MAP:${CMAKE_CURRENT_BINARY_DIR}/de_restoration_resources.map")
endif()
target_include_directories(de_restoration_resources PRIVATE "${PROJECT_SOURCE_DIR}/src/fft")
if(APPLE AND DE_ENABLE_ASAN)
  # The preceding selection validates this configured runtime; retain its order before observer.
  target_link_libraries(de_restoration_resources PRIVATE "${de_asan_selected}")
endif()
target_link_libraries(de_restoration_resources PRIVATE de_methods de_fft de_image de_exec de_core de_fft_allocation_observer)
de_apply_options(de_restoration_resources)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(de_restoration_resources PRIVATE "LINKER:--wrap=malloc" "LINKER:--wrap=free")
endif()
# One source authority for compiled process partitioning and required CTest inventory.
set(de_restoration_resource_shards 16)
target_compile_definitions(de_restoration_resources PRIVATE
  DE_RESTORATION_RESOURCE_SHARDS=${de_restoration_resource_shards})
math(EXPR de_restoration_last_shard "${de_restoration_resource_shards} - 1")
foreach(de_shard RANGE 0 ${de_restoration_last_shard})
  add_test(NAME restoration-native-resources-${de_shard}
    COMMAND de_restoration_resources --shard ${de_shard})
  set_tests_properties(restoration-native-resources-${de_shard} PROPERTIES TIMEOUT 90)
endforeach()
add_test(NAME restoration-native-controls COMMAND de_restoration_resources --controls)
set_tests_properties(restoration-native-controls PROPERTIES TIMEOUT 90)

# The admitted R5 numerical case is explicit, never part of routine CTest or a campaign.
add_executable(de_restoration_numerics support/restoration_numerics.cpp)
target_include_directories(de_restoration_numerics PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(de_restoration_numerics PRIVATE de_methods de_image de_exec de_core)
de_apply_options(de_restoration_numerics)
