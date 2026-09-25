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
  HEADERS "${DE_INCLUDE}/core/memory.hpp" "${DE_INCLUDE}/core/result.hpp"
          "${DE_INCLUDE}/core/cancellation.hpp")
de_library(de_contract LAYER contract
  SOURCES contract/parse.cpp
  HEADERS "${DE_INCLUDE}/contract/command.hpp" "${DE_INCLUDE}/contract/cli_contract.hpp"
          "${DE_INCLUDE}/contract/parse.hpp" "${DE_INCLUDE}/contract/utf8.hpp"
  LINKS de_core)
de_library(de_exec LAYER exec
  SOURCES exec/concurrency.cpp exec/scheduler.cpp
  HEADERS "${DE_INCLUDE}/exec/concurrency.hpp" "${DE_INCLUDE}/exec/scheduler.hpp"
  LINKS de_core)
de_library(de_image LAYER image
  SOURCES image/numeric.cpp image/plane.cpp image/raster.cpp image/continuous.cpp image/linear.cpp
  HEADERS "${DE_INCLUDE}/image/numeric.hpp" "${DE_INCLUDE}/image/plane.hpp"
          "${DE_INCLUDE}/image/raster.hpp" "${DE_INCLUDE}/image/continuous.hpp" "${DE_INCLUDE}/image/linear.hpp"
  LINKS de_core)
de_library(de_methods LAYER methods
  SOURCES methods/box_mean.cpp methods/catalog.cpp methods/fixed_threshold.cpp methods/binarization.cpp methods/sauvola.cpp
          methods/illumination.cpp methods/surface_detail.hpp methods/surface_grid.cpp
          methods/surface_measure.cpp methods/surface_system.cpp methods/surface_solve.cpp
          methods/surface_samples.cpp methods/surface_fit.cpp methods/surface_apply.cpp
  HEADERS "${DE_INCLUDE}/methods/box_mean.hpp" "${DE_INCLUDE}/methods/catalog.hpp"
          "${DE_INCLUDE}/methods/fixed_threshold.hpp" "${DE_INCLUDE}/methods/binarization.hpp"
          "${DE_INCLUDE}/methods/sauvola.hpp" "${DE_INCLUDE}/methods/method_catalog.hpp"
          "${DE_INCLUDE}/methods/illumination.hpp" "${DE_INCLUDE}/methods/surface.hpp"
  LINKS de_core de_exec de_image)
de_library(de_app LAYER app
  SOURCES app/dispatch.cpp app/process.cpp app/illumination.cpp app/illumination.hpp
  HEADERS "${DE_INCLUDE}/app/dispatch.hpp" "${DE_INCLUDE}/app/process.hpp"
  LINKS de_core de_contract de_methods de_image)
target_include_directories(de_app PRIVATE "${PROJECT_BINARY_DIR}/generated")
de_library(de_report LAYER report
  SOURCES report/render.cpp report/continuous.cpp report/continuous.hpp report/illumination.cpp report/illumination.hpp HEADERS "${DE_INCLUDE}/report/render.hpp"
  LINKS de_app de_contract de_core de_image de_methods nlohmann_json::nlohmann_json)
de_library(de_cli LAYER cli
  SOURCES cli/run.cpp HEADERS "${DE_INCLUDE}/cli/run.hpp"
  LINKS de_app de_contract de_core de_report CLI11::CLI11)

de_library(de_io LAYER io
  SOURCES io/png_context.cpp io/png_decode.cpp io/png_file.cpp io/png_encode.cpp io/png_publish.cpp
          io/publication.cpp io/png_context.hpp io/png_reader.hpp io/publication.hpp
          io/png_metadata.cpp io/png_metadata.hpp io/png_profile.cpp io/png_exif.cpp
          io/png_raster.cpp io/png_snapshot.cpp io/png_rows.hpp io/png_write_rows.cpp
          io/png_inventory.cpp io/png_verify_rows.cpp io/png_publish_rows.cpp
          io/png_protection.cpp io/png_snapshot.hpp
  HEADERS "${DE_INCLUDE}/io/png.hpp" "${DE_INCLUDE}/io/continuous_png.hpp" "${DE_INCLUDE}/io/protection_png.hpp"
  LINKS de_core de_image DE::png DE::zlib)
de_library(de_color LAYER color
  SOURCES color/context.cpp color/context.hpp color/profiles.cpp color/interpretation.cpp
          color/converter.cpp color/conversion.hpp color/prepare.cpp color/rows.cpp
  HEADERS "${DE_INCLUDE}/color/converter.hpp"
  LINKS de_core de_image DE::lcms)
# Both fuzz groups compile production targets. Only codec harnesses link io; the CLI closure
# remains pure and never composes the host or publication path.
if(DE_FUZZ_ONLY)
  de_write_architecture_targets("${PROJECT_BINARY_DIR}/architecture-targets.json" ROOTS cli io color)
  return()
endif()
de_library(de_host LAYER host
  SOURCES host/processor.cpp host/continuous.cpp host/continuous.hpp
          host/illumination_rows.cpp host/illumination_rows.hpp HEADERS "${DE_INCLUDE}/host/processor.hpp"
  LINKS de_app de_core de_exec de_image de_io de_methods de_color)
add_executable(docenhance entry/main.cpp entry/signals.cpp entry/signals.hpp)
if(MINGW)
  target_link_options(docenhance PRIVATE -municode)
endif()
target_link_libraries(docenhance PRIVATE de_cli de_core de_host)
de_apply_options(docenhance)
set_target_properties(docenhance PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
de_register_target(docenhance LAYER entry SOURCES entry/main.cpp entry/signals.cpp entry/signals.hpp LINKS de_cli de_core de_host)
de_write_architecture_targets("${PROJECT_BINARY_DIR}/architecture-targets.json")

# Not a layer: this probe exists to prove the pinned native dependencies build, link and run, so it
# links libraries it does not otherwise use. It never ships and nothing in src/ may depend on it.
if(DE_BUILD_TOOLS)
  add_executable(de_native_probe "${PROJECT_SOURCE_DIR}/tools/native_probe.cpp")
  target_link_libraries(de_native_probe PRIVATE opencv_photo opencv_core DE::leptonica DE::lcms DE::jpeg DE::png DE::tiff DE::picosha2)
  de_apply_options(de_native_probe)
endif()
