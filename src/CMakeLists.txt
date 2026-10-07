# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
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
  SOURCES core/memory.cpp core/identity.cpp
  HEADERS "${DE_INCLUDE}/core/identity.hpp" "${DE_INCLUDE}/core/memory.hpp" "${DE_INCLUDE}/core/result.hpp"
          "${DE_INCLUDE}/core/cancellation.hpp" "${DE_INCLUDE}/core/utf8.hpp"
          "${DE_INCLUDE}/core/bundle_path.hpp" "${DE_INCLUDE}/core/limits.hpp")
target_include_directories(de_core PRIVATE "${PROJECT_BINARY_DIR}/generated")
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
  SOURCES image/gaussian.cpp image/numeric.cpp image/ordered_samples.cpp image/perceptual.cpp image/plane_rows.cpp image/plane.cpp image/raster.cpp image/continuous.cpp image/linear.cpp image/source.cpp image/tiff_source.cpp
  HEADERS "${DE_INCLUDE}/image/gaussian.hpp" "${DE_INCLUDE}/image/numeric.hpp" "${DE_INCLUDE}/image/plane.hpp"
          "${DE_INCLUDE}/image/raster.hpp" "${DE_INCLUDE}/image/source.hpp" "${DE_INCLUDE}/image/continuous.hpp" "${DE_INCLUDE}/image/linear.hpp"
  LINKS de_core)
de_library(de_methods LAYER methods
  SOURCES methods/box_mean.cpp methods/catalog.cpp methods/fixed_threshold.cpp methods/binarization.cpp methods/sauvola.cpp
          methods/denoising.cpp methods/contrast.cpp methods/contrast_model.cpp methods/contrast_measure.cpp methods/clahe_measure.cpp methods/clahe_apply.cpp methods/contrast_apply.cpp methods/contrast_validation.cpp methods/contrast_detail.hpp methods/illumination.cpp methods/surface_detail.hpp methods/surface_grid.cpp
          methods/surface_measure.cpp methods/surface_system.cpp methods/surface_multigrid.cpp methods/surface_solve.cpp
          methods/surface_samples.cpp methods/surface_fit.cpp methods/surface_apply.cpp
          methods/illumination_apply.cpp methods/morphology_extrema.cpp methods/sharpening.cpp methods/sharpen_model.cpp methods/sharpen_apply.cpp methods/sharpen_validation.cpp
          methods/morphology_detail.hpp methods/morphology_measure.cpp methods/morphology_statistics.cpp
          methods/morphology_model.cpp methods/morphology_apply.cpp methods/morphology_validation.cpp methods/tvl1_iteration.cpp methods/tvl1_operators.hpp
          methods/tvl1_objective.cpp methods/tvl1_model.cpp methods/tvl1_apply.cpp methods/tvl1_validation.cpp
  HEADERS "${DE_INCLUDE}/methods/box_mean.hpp" "${DE_INCLUDE}/methods/catalog.hpp"
          "${DE_INCLUDE}/methods/fixed_threshold.hpp" "${DE_INCLUDE}/methods/binarization.hpp"
          "${DE_INCLUDE}/methods/sauvola.hpp" "${DE_INCLUDE}/methods/reviewed_methods.hpp"
          "${DE_INCLUDE}/methods/denoising.hpp" "${DE_INCLUDE}/methods/sharpening.hpp" "${DE_INCLUDE}/methods/contrast.hpp" "${DE_INCLUDE}/methods/illumination.hpp" "${DE_INCLUDE}/methods/surface.hpp" "${DE_INCLUDE}/methods/morphology.hpp" "${DE_INCLUDE}/methods/tvl1.hpp"
  LINKS de_core de_exec de_image)
de_library(de_app LAYER app
  SOURCES app/observations.cpp app/observations.hpp app/denoising.cpp app/sharpening.cpp app/sharpening.hpp app/contrast.cpp app/contrast.hpp app/denoising.hpp app/dispatch.cpp app/verify.cpp app/process.cpp app/illumination.cpp app/illumination.hpp
  HEADERS "${DE_INCLUDE}/app/dispatch.hpp" "${DE_INCLUDE}/app/process.hpp"
          "${DE_INCLUDE}/app/verify.hpp"
  LINKS de_core de_contract de_methods de_image)
target_include_directories(de_app PRIVATE "${PROJECT_BINARY_DIR}/generated")
de_library(de_bundle LAYER bundle
  SOURCES bundle/morphology_fields.cpp bundle/denoising_fields.cpp bundle/sharpen_fields.cpp bundle/read_sharpen.cpp bundle/contrast_fields.cpp bundle/read_contrast.cpp bundle/tvl1_fields.cpp bundle/read_denoising.cpp bundle/fields.cpp bundle/record.cpp bundle/inventory.cpp bundle/read_fields.hpp bundle/read_values.cpp bundle/read_json.cpp
          bundle/read_observations.cpp bundle/read_operation.cpp bundle/read_illumination.cpp bundle/read_claims.cpp bundle/source_fields.cpp bundle/read_source.cpp
  HEADERS "${DE_INCLUDE}/bundle/fields.hpp" "${DE_INCLUDE}/bundle/record.hpp"
          "${DE_INCLUDE}/bundle/inventory.hpp"
  LINKS de_core de_image de_methods nlohmann_json::nlohmann_json)
de_library(de_report LAYER report
  SOURCES report/render.cpp report/continuous.cpp report/continuous.hpp report/illumination.cpp report/denoising.cpp report/sharpening.cpp report/sharpening.hpp report/contrast.cpp report/contrast.hpp report/denoising.hpp report/illumination.hpp HEADERS "${DE_INCLUDE}/report/render.hpp"
  LINKS de_app de_bundle de_contract de_core de_image de_methods nlohmann_json::nlohmann_json)
de_library(de_cli LAYER cli
  SOURCES cli/run.cpp cli/failures.hpp HEADERS "${DE_INCLUDE}/cli/run.hpp"
  LINKS de_app de_contract de_core de_report CLI11::CLI11)

de_library(de_io LAYER io
  SOURCES io/file_stream.cpp io/png_context.cpp io/png_decode.cpp io/png_file.cpp io/png_encode.cpp io/png_write_image.cpp io/bundle_publish.cpp io/bundle_stage.cpp io/bundle_stage.hpp io/bundle_inspect.cpp io/png_artifact.cpp io/entry_identity.hpp io/entry_identity.cpp io/bundle_native.hpp io/bundle_posix.cpp io/bundle_windows.cpp io/windows_sdk.hpp
          io/publication.cpp io/png_context.hpp io/png_reader.hpp io/native_publication.hpp
          io/png_metadata.cpp io/png_metadata.hpp io/png_chunks.cpp io/png_chunks.hpp io/png_profile.cpp io/exif.cpp io/exif.hpp
          io/tiff_layout.hpp io/tiff_layout.cpp io/tiff_ifd.hpp io/tiff_ifd.cpp io/tiff_context.hpp io/tiff_context.cpp io/tiff_decode.cpp io/tiff_tags.cpp io/tiff_pixels.cpp io/jpeg_context.hpp io/jpeg_context.cpp io/jpeg_memory.cpp io/jpeg_scan.hpp io/jpeg_markers.hpp io/jpeg_scan.cpp io/jpeg_metadata.cpp io/jpeg_decode.cpp io/source.cpp
          io/png_raster.cpp io/source_snapshot.cpp io/png_rows.hpp io/png_write_rows.cpp
          io/png_inventory.cpp io/png_verify_rows.cpp io/png_verified_rows.cpp
          io/png_protection.cpp io/source_snapshot.hpp io/digest.cpp io/slot_identity.cpp io/file_access.hpp
  HEADERS "${DE_INCLUDE}/io/png.hpp" "${DE_INCLUDE}/io/continuous_png.hpp" "${DE_INCLUDE}/io/source.hpp" "${DE_INCLUDE}/io/jpeg.hpp" "${DE_INCLUDE}/io/tiff.hpp"
          "${DE_INCLUDE}/io/protection_png.hpp" "${DE_INCLUDE}/io/digest.hpp" "${DE_INCLUDE}/io/publication.hpp" "${DE_INCLUDE}/io/bundle_snapshot.hpp"
          "${DE_INCLUDE}/io/png_artifact.hpp" "${DE_INCLUDE}/io/artifact_limits.hpp" "${DE_INCLUDE}/io/paths.hpp"
  LINKS de_core de_image DE::png DE::jpeg DE::tiff DE::zlib DE::picosha2)
de_library(de_color LAYER color
  SOURCES color/context.cpp color/context.hpp color/profiles.cpp color/interpretation.cpp
          color/converter.cpp color/conversion.hpp color/prepare.cpp color/rows.cpp
  HEADERS "${DE_INCLUDE}/color/converter.hpp" "${DE_INCLUDE}/color/profile.hpp"
  LINKS de_core de_image DE::lcms)
de_library(de_denoise LAYER denoise
  SOURCES denoise/native.cpp denoise/tiles.cpp
  HEADERS "${DE_INCLUDE}/denoise/nlm.hpp"
  LINKS de_core de_image de_methods opencv_core opencv_photo)
# Both fuzz groups compile production targets. Only codec harnesses link io; the CLI closure
# remains pure and never composes the host or publication path.
if(DE_FUZZ_ONLY)
  de_write_architecture_targets("${PROJECT_BINARY_DIR}/architecture-targets.json" ROOTS cli io color denoise)
  return()
endif()
de_library(de_host LAYER host
  SOURCES host/run_publication.cpp host/run_publication.hpp host/bundle_verify.cpp host/bundle_verify.hpp host/context.cpp host/instant.cpp host/instant.hpp host/verifier.cpp host/processor.cpp host/stage_reports.cpp host/stage_reports.hpp host/continuous.cpp host/continuous.hpp
          host/linear_rows.cpp host/linear_rows.hpp host/denoising.cpp host/sharpening.cpp host/sharpening.hpp host/contrast.cpp host/contrast.hpp host/denoising.hpp HEADERS "${DE_INCLUDE}/host/processor.hpp" "${DE_INCLUDE}/host/verifier.hpp"
  LINKS de_app de_bundle de_core de_exec de_image de_io de_methods de_color de_denoise)
add_executable(docenhance entry/main.cpp entry/signals.cpp entry/signals.hpp)
if(MINGW)
  target_link_options(docenhance PRIVATE -municode)
endif()
target_link_libraries(docenhance PRIVATE de_cli de_core de_host)
de_apply_options(docenhance)
set_target_properties(docenhance PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${DE_BUILD_OWNER}/bin")
de_register_target(docenhance LAYER entry SOURCES entry/main.cpp entry/signals.cpp entry/signals.hpp LINKS de_cli de_core de_host)
de_write_architecture_targets("${PROJECT_BINARY_DIR}/architecture-targets.json")

# Not a layer: this probe exists to prove the pinned native dependencies build, link and run, so it
# links libraries it does not otherwise use. It never ships and nothing in src/ may depend on it.
if(DE_BUILD_TOOLS)
  add_executable(de_native_probe "${PROJECT_SOURCE_DIR}/tools/native_probe.cpp")
  target_link_libraries(de_native_probe PRIVATE opencv_photo opencv_core DE::leptonica DE::lcms DE::jpeg DE::png DE::tiff DE::picosha2)
  de_apply_options(de_native_probe)
endif()
