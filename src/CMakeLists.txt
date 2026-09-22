# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
# Every target here declares the layer it belongs to. cmake/ArchitectureChecks.cmake checks each
# link against spec/architecture.json, so the allowed edges are stated once, in the manifest.
set(DE_INCLUDE "${PROJECT_SOURCE_DIR}/include/docenhance")

function(de_library name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "LAYER" "SOURCES;HEADERS;LINKS")
  add_library(${name} STATIC ${arg_SOURCES})
  target_sources(${name} PUBLIC FILE_SET HEADERS BASE_DIRS "${PROJECT_SOURCE_DIR}/include" FILES ${arg_HEADERS})
  target_include_directories(${name} PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>")
  target_link_libraries(${name} PRIVATE ${arg_LINKS})
  de_apply_options(${name})
  if(DE_FUZZ_ONLY)
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer-no-link)
  endif()
  de_register_target(${name} LAYER ${arg_LAYER} SOURCES ${arg_SOURCES} HEADERS ${arg_HEADERS} LINKS ${arg_LINKS})
endfunction()

de_library(de_core LAYER core
  SOURCES core/memory.cpp
  HEADERS "${DE_INCLUDE}/core/memory.hpp" "${DE_INCLUDE}/core/result.hpp")
de_library(de_contract LAYER contract
  SOURCES contract/parse.cpp
  HEADERS "${DE_INCLUDE}/contract/command.hpp" "${DE_INCLUDE}/contract/cli_contract.hpp"
          "${DE_INCLUDE}/contract/parse.hpp"
  LINKS de_core)
de_library(de_exec LAYER exec
  SOURCES exec/concurrency.cpp exec/scheduler.cpp
  HEADERS "${DE_INCLUDE}/exec/concurrency.hpp" "${DE_INCLUDE}/exec/scheduler.hpp"
  LINKS de_core)
de_library(de_image LAYER image
  SOURCES image/numeric.cpp image/plane.cpp
  HEADERS "${DE_INCLUDE}/image/numeric.hpp" "${DE_INCLUDE}/image/plane.hpp"
  LINKS de_core)
de_library(de_methods LAYER methods
  SOURCES methods/box_mean.cpp methods/catalog.cpp methods/fixed_threshold.cpp
  HEADERS "${DE_INCLUDE}/methods/box_mean.hpp" "${DE_INCLUDE}/methods/catalog.hpp"
          "${DE_INCLUDE}/methods/fixed_threshold.hpp"
  LINKS de_core de_exec de_image)
de_library(de_app LAYER app
  SOURCES app/dispatch.cpp app/process.cpp
  HEADERS "${DE_INCLUDE}/app/dispatch.hpp" "${DE_INCLUDE}/app/process.hpp"
  LINKS de_core de_contract de_methods)
target_include_directories(de_app PRIVATE "${PROJECT_BINARY_DIR}/generated")
de_library(de_report LAYER report
  SOURCES report/render.cpp HEADERS "${DE_INCLUDE}/report/render.hpp"
  LINKS de_app de_contract de_core nlohmann_json::nlohmann_json)
de_library(de_cli LAYER cli
  SOURCES cli/run.cpp HEADERS "${DE_INCLUDE}/cli/run.hpp"
  LINKS de_app de_contract de_core de_report CLI11::CLI11)

# Fuzzing compiles the SAME first-party targets and their real transitive links, but deliberately
# never builds the concrete host or codecs. The CLI harness supplies an in-memory processing port.
if(DE_FUZZ_ONLY)
  de_write_architecture_targets("${PROJECT_BINARY_DIR}/architecture-targets.json" ROOTS cli)
  return()
endif()
de_library(de_io LAYER io
  SOURCES io/png_context.cpp io/png_decode.cpp io/png_encode.cpp io/png_publish.cpp
          io/publication.cpp io/png_context.hpp io/publication.hpp
  HEADERS "${DE_INCLUDE}/io/png.hpp"
  LINKS de_core de_image DE::png)
de_library(de_host LAYER host
  SOURCES host/processor.cpp HEADERS "${DE_INCLUDE}/host/processor.hpp"
  LINKS de_app de_core de_image de_io de_methods)
add_executable(docenhance entry/main.cpp)
if(MINGW)
  target_link_options(docenhance PRIVATE -municode)
endif()
target_link_libraries(docenhance PRIVATE de_cli de_core de_host)
de_apply_options(docenhance)
set_target_properties(docenhance PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
de_register_target(docenhance LAYER entry SOURCES entry/main.cpp LINKS de_cli de_core de_host)
de_write_architecture_targets("${PROJECT_BINARY_DIR}/architecture-targets.json")

# Not a layer: this probe exists to prove the pinned native dependencies build, link and run, so it
# links libraries it does not otherwise use. It never ships and nothing in src/ may depend on it.
if(DE_BUILD_TOOLS)
  add_executable(de_native_probe "${PROJECT_SOURCE_DIR}/tools/native_probe.cpp")
  target_link_libraries(de_native_probe PRIVATE opencv_photo opencv_core DE::leptonica DE::lcms DE::jpeg DE::png DE::tiff DE::picosha2)
  de_apply_options(de_native_probe)
endif()
