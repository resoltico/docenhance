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
de_library(de_exec LAYER exec
  SOURCES exec/concurrency.cpp exec/scheduler.cpp
  HEADERS "${DE_INCLUDE}/exec/concurrency.hpp" "${DE_INCLUDE}/exec/scheduler.hpp"
  LINKS de_core)
de_library(de_image LAYER image
  SOURCES image/geometry.cpp image/gaussian.cpp image/numeric.cpp image/ordered_samples.cpp image/perceptual.cpp image/plane_rows.cpp image/plane.cpp image/raster.cpp image/continuous.cpp image/linear.cpp image/source.cpp image/tiff_source.cpp
  HEADERS "${DE_INCLUDE}/image/geometry.hpp" "${DE_INCLUDE}/image/gaussian.hpp" "${DE_INCLUDE}/image/numeric.hpp" "${DE_INCLUDE}/image/plane.hpp"
          "${DE_INCLUDE}/image/raster.hpp" "${DE_INCLUDE}/image/source_facts.hpp" "${DE_INCLUDE}/image/continuous.hpp" "${DE_INCLUDE}/image/linear.hpp"
  LINKS de_core)
de_library(de_methods LAYER methods
  SOURCES methods/box_mean.cpp methods/catalog.cpp methods/fixed_threshold.cpp methods/otsu.cpp methods/binarization.cpp methods/sauvola.cpp
          methods/restoration.cpp methods/restoration_psf.cpp methods/restoration_validation.cpp methods/restoration_resources.cpp methods/denoising.cpp methods/contrast.cpp methods/contrast_model.cpp methods/contrast_measure.cpp methods/clahe_measure.cpp methods/clahe_apply.cpp methods/contrast_apply.cpp methods/contrast_validation.cpp methods/contrast_detail.hpp methods/illumination.cpp methods/surface_detail.hpp methods/surface_grid.cpp
          methods/surface_measure.cpp methods/surface_system.cpp methods/surface_multigrid.cpp methods/surface_solve.cpp
          methods/surface_samples.cpp methods/surface_fit.cpp methods/surface_apply.cpp
          methods/illumination_apply.cpp methods/morphology_extrema.cpp methods/sharpening.cpp methods/sharpen_model.cpp methods/sharpen_apply.cpp methods/sharpen_validation.cpp
          methods/morphology_detail.hpp methods/morphology_measure.cpp methods/morphology_statistics.cpp
          methods/morphology_model.cpp methods/morphology_apply.cpp methods/morphology_validation.cpp methods/tvl1_iteration.cpp methods/tvl1_operators.hpp
          methods/tvl1_objective.cpp methods/tvl1_model.cpp methods/tvl1_apply.cpp methods/tvl1_validation.cpp
  HEADERS "${DE_INCLUDE}/methods/box_mean.hpp" "${DE_INCLUDE}/methods/catalog.hpp"
          "${DE_INCLUDE}/methods/fixed_threshold.hpp" "${DE_INCLUDE}/methods/binarization.hpp"
          "${DE_INCLUDE}/methods/stages.hpp" "${DE_INCLUDE}/methods/otsu.hpp" "${DE_INCLUDE}/methods/sauvola.hpp" "${DE_INCLUDE}/methods/reviewed_methods.hpp"
          "${DE_INCLUDE}/methods/restoration.hpp" "${DE_INCLUDE}/methods/denoising.hpp" "${DE_INCLUDE}/methods/sharpening.hpp" "${DE_INCLUDE}/methods/contrast.hpp" "${DE_INCLUDE}/methods/progress.hpp" "${DE_INCLUDE}/methods/illumination.hpp" "${DE_INCLUDE}/methods/surface.hpp" "${DE_INCLUDE}/methods/morphology.hpp" "${DE_INCLUDE}/methods/tvl1.hpp"
  LINKS de_core de_exec de_image)
de_library(de_app LAYER app
  SOURCES app/values.cpp app/stage_observations.cpp app/verification_observations.cpp app/restoration.cpp app/restoration.hpp app/observations.cpp app/observations.hpp app/denoising.cpp app/sharpening.cpp app/sharpening.hpp app/contrast.cpp app/contrast.hpp app/denoising.hpp app/dispatch.cpp app/verify.cpp app/process.cpp app/illumination.cpp app/illumination.hpp
  HEADERS "${DE_INCLUDE}/app/dispatch.hpp" "${DE_INCLUDE}/app/process.hpp"
          "${DE_INCLUDE}/app/verify.hpp" "${DE_INCLUDE}/app/invocation.hpp"
          "${DE_INCLUDE}/app/values.hpp" "${DE_INCLUDE}/app/cli_contract.hpp"
  LINKS de_core de_methods de_image)
target_include_directories(de_app PRIVATE "${PROJECT_BINARY_DIR}/generated")
de_library(de_record LAYER record
  SOURCES record/restoration_fields.cpp record/read_restoration.cpp record/morphology_fields.cpp record/denoising_fields.cpp record/sharpen_fields.cpp record/read_sharpen.cpp record/contrast_fields.cpp record/read_contrast.cpp record/tvl1_fields.cpp record/read_denoising.cpp record/fields.cpp record/record.cpp record/inventory.cpp record/read_fields.hpp record/read_values.cpp record/read_json.cpp
          record/read_observations.cpp record/read_operation.cpp record/read_illumination.cpp record/read_claims.cpp record/source_fields.cpp record/read_source.cpp
  HEADERS "${DE_INCLUDE}/record/fields.hpp" "${DE_INCLUDE}/record/record.hpp"
          "${DE_INCLUDE}/record/record_version.hpp"
          "${DE_INCLUDE}/record/inventory.hpp"
  LINKS de_core de_image de_methods nlohmann_json::nlohmann_json)
de_library(de_report LAYER report
  SOURCES report/help.cpp report/help.hpp report/restoration.cpp report/restoration.hpp report/render.cpp report/continuous.cpp report/continuous.hpp report/illumination.cpp report/denoising.cpp report/sharpening.cpp report/sharpening.hpp report/contrast.cpp report/contrast.hpp report/denoising.hpp report/illumination.hpp HEADERS "${DE_INCLUDE}/report/render.hpp"
  LINKS de_app de_record de_core de_image de_methods nlohmann_json::nlohmann_json)
de_library(de_cli LAYER cli
  SOURCES cli/run.cpp cli/failures.hpp HEADERS "${DE_INCLUDE}/cli/run.hpp"
  LINKS de_app de_core de_report CLI11::CLI11)

de_library(de_codec LAYER codec
  SOURCES codec/exif.cpp codec/exif.hpp codec/jpeg_context.cpp codec/jpeg_context.hpp
          codec/jpeg_decode.cpp codec/jpeg_markers.hpp codec/jpeg_memory.cpp codec/jpeg_metadata.cpp
          codec/jpeg_scan.cpp codec/jpeg_scan.hpp codec/png_artifact.cpp codec/png_chunks.cpp
          codec/png_chunks.hpp codec/png_context.cpp codec/png_context.hpp codec/png_decode.cpp
          codec/png_encode.cpp codec/png_inventory.cpp codec/png_metadata.cpp codec/png_metadata.hpp
          codec/png_profile.cpp codec/png_protection.cpp codec/png_psf.cpp codec/png_raster.cpp
          codec/png_reader.hpp codec/png_rows.hpp codec/png_verify_rows.cpp codec/png_write_rows.cpp
          codec/source.cpp codec/tiff_context.cpp codec/tiff_context.hpp codec/tiff_decode.cpp
          codec/tiff_ifd.cpp codec/tiff_ifd.hpp codec/tiff_layout.cpp codec/tiff_layout.hpp
          codec/tiff_pixels.cpp codec/tiff_tags.cpp
  HEADERS "${DE_INCLUDE}/codec/continuous_png.hpp" "${DE_INCLUDE}/codec/jpeg.hpp"
          "${DE_INCLUDE}/codec/png.hpp" "${DE_INCLUDE}/codec/png_artifact.hpp"
          "${DE_INCLUDE}/codec/png_stream.hpp" "${DE_INCLUDE}/codec/protection_png.hpp"
          "${DE_INCLUDE}/codec/psf_png.hpp" "${DE_INCLUDE}/codec/source.hpp"
          "${DE_INCLUDE}/codec/tiff.hpp"
  LINKS de_core de_image DE::png DE::jpeg DE::tiff DE::zlib)
if(NOT DE_FUZZ_ONLY)
de_library(de_fs LAYER fs
  SOURCES fs/bundle_inspect.cpp fs/bundle_native.hpp fs/bundle_posix.cpp fs/bundle_publish.cpp
          fs/bundle_stage.cpp fs/bundle_stage.hpp fs/bundle_windows.cpp fs/digest.cpp
          fs/entry_identity.cpp fs/entry_identity.hpp fs/file_access.hpp fs/file_stream.cpp
          fs/native_publication.hpp fs/png_file.cpp fs/png_output.cpp fs/png_file_rows.hpp
          fs/png_verified_rows.cpp fs/png_verify.cpp fs/png_write_image.cpp fs/protection_png.cpp
          fs/psf_png.cpp fs/publication.cpp fs/slot_identity.cpp fs/source.cpp
          fs/source_snapshot.cpp fs/source_snapshot.hpp fs/windows_sdk.hpp
  HEADERS "${DE_INCLUDE}/fs/artifact_limits.hpp" "${DE_INCLUDE}/fs/bundle_snapshot.hpp"
          "${DE_INCLUDE}/fs/digest.hpp" "${DE_INCLUDE}/fs/paths.hpp"
          "${DE_INCLUDE}/fs/png.hpp" "${DE_INCLUDE}/fs/protection_png.hpp"
          "${DE_INCLUDE}/fs/psf_png.hpp" "${DE_INCLUDE}/fs/publication.hpp"
          "${DE_INCLUDE}/fs/source.hpp"
  LINKS de_codec de_core de_image DE::picosha2)
endif()
de_library(de_color LAYER color
  SOURCES color/context.cpp color/context.hpp color/profiles.cpp color/interpretation.cpp
          color/converter.cpp color/conversion.hpp color/prepare.cpp color/rows.cpp
  HEADERS "${DE_INCLUDE}/color/converter.hpp" "${DE_INCLUDE}/color/profile.hpp"
  LINKS de_core de_image DE::lcms)
de_library(de_opencv LAYER opencv
  SOURCES opencv/restoration_allocate.cpp opencv/restoration_pad.cpp opencv/restoration_native.cpp opencv/restoration_model.cpp opencv/restoration_apply.cpp opencv/restoration_work.hpp opencv/nlm_native.cpp opencv/nlm_tiles.cpp opencv/diagnostics.cpp opencv/diagnostics.hpp
  HEADERS "${DE_INCLUDE}/opencv/restoration.hpp" "${DE_INCLUDE}/opencv/nlm.hpp"
  LINKS de_core de_image de_methods opencv_core opencv_photo)
# Both fuzz groups compile production targets. Only codec harnesses link codec; the CLI closure
# remains pure and never composes the host or publication path.
if(DE_FUZZ_ONLY)
  de_write_architecture_targets("${PROJECT_BINARY_DIR}/architecture-targets.json" ROOTS cli codec color opencv)
  return()
endif()
de_library(de_host LAYER host
  SOURCES host/protection.cpp host/protection.hpp host/run_publication.cpp host/run_publication.hpp host/bundle_verify.cpp host/bundle_verify.hpp host/context.cpp host/instant.cpp host/instant.hpp host/verifier.cpp host/processor.cpp host/continuous.cpp host/continuous.hpp
          host/restoration.cpp host/restoration.hpp host/linear_rows.cpp host/linear_rows.hpp host/denoising.cpp host/sharpening.cpp host/sharpening.hpp host/contrast.cpp host/contrast.hpp host/denoising.hpp HEADERS "${DE_INCLUDE}/host/processor.hpp" "${DE_INCLUDE}/host/verifier.hpp"
  LINKS de_app de_record de_core de_exec de_image de_codec de_fs de_methods de_color de_opencv)
add_executable(docenhance entry/main.cpp entry/signals.cpp entry/signals.hpp)
if(MINGW)
  target_link_options(docenhance PRIVATE -municode)
endif()
target_link_libraries(docenhance PRIVATE de_cli de_core de_host)
de_apply_options(docenhance)
set_target_properties(docenhance PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${DE_BUILD_OWNER}/bin")
de_register_target(docenhance LAYER entry SOURCES entry/main.cpp entry/signals.cpp entry/signals.hpp LINKS de_cli de_core de_host)
de_write_architecture_targets("${PROJECT_BINARY_DIR}/architecture-targets.json")
