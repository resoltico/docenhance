# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
# Every target here declares the layer it belongs to. cmake/ArchitectureChecks.cmake checks each
# link against spec/architecture.json, so the allowed edges are stated once, in the manifest.
set(DE_INCLUDE "${PROJECT_SOURCE_DIR}/include/docenhance")

function(de_library name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "LAYER" "SOURCES;HEADERS;LINKS;PUBLIC_LINKS")
  add_library(${name} STATIC ${arg_SOURCES})
  target_sources(${name} PUBLIC FILE_SET HEADERS BASE_DIRS "${PROJECT_SOURCE_DIR}/include" FILES ${arg_HEADERS})
  target_include_directories(${name} PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>")
  target_link_libraries(${name} PRIVATE ${arg_LINKS} PUBLIC ${arg_PUBLIC_LINKS})
  de_apply_options(${name})
  if(DE_FUZZ_ONLY)
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer-no-link)
  endif()
  de_register_target(${name} LAYER ${arg_LAYER} SOURCES ${arg_SOURCES} HEADERS ${arg_HEADERS} LINKS ${arg_LINKS} ${arg_PUBLIC_LINKS})
endfunction()

de_library(de_core LAYER core
  SOURCES core/memory.cpp core/identity.cpp
  HEADERS "${DE_INCLUDE}/core/identity.hpp" "${DE_INCLUDE}/core/memory.hpp" "${DE_INCLUDE}/core/result.hpp"
          "${DE_INCLUDE}/core/cancellation.hpp" "${DE_INCLUDE}/core/utf8.hpp"
          "${DE_INCLUDE}/core/bundle_path.hpp" "${DE_INCLUDE}/core/limits.hpp"
          "${DE_INCLUDE}/core/execution_limits.hpp"
          "${DE_INCLUDE}/core/range.hpp" "${DE_INCLUDE}/core/fields.hpp" "${DE_INCLUDE}/core/warnings.hpp")
target_include_directories(de_core PRIVATE "${PROJECT_BINARY_DIR}/generated")
de_library(de_exec LAYER exec
  SOURCES exec/concurrency.cpp exec/scheduler.cpp
  HEADERS "${DE_INCLUDE}/exec/concurrency.hpp" "${DE_INCLUDE}/exec/scheduler.hpp"
  LINKS de_core)
de_library(de_image LAYER image
  SOURCES image/warnings.cpp image/geometry.cpp image/gaussian.cpp image/numeric.cpp image/ordered_samples.cpp image/perceptual.cpp image/plane.cpp image/raster.cpp image/output.cpp image/source.cpp image/tiff_source.cpp
  HEADERS "${DE_INCLUDE}/image/geometry.hpp" "${DE_INCLUDE}/image/gaussian.hpp" "${DE_INCLUDE}/image/numeric.hpp" "${DE_INCLUDE}/image/plane.hpp"
          "${DE_INCLUDE}/image/output.hpp" "${DE_INCLUDE}/image/raster.hpp" "${DE_INCLUDE}/image/source_facts.hpp" "${DE_INCLUDE}/image/warnings.hpp"
  LINKS de_core)
de_library(de_fft LAYER fft
  SOURCES fft/fft.cpp fft/fft_detail.hpp fft/spectrum.cpp fft/workspace.cpp fft/pocketfft.cpp
          fft/validation.cpp fft/phases.cpp
  HEADERS "${DE_INCLUDE}/fft/fft.hpp"
  LINKS de_core de_exec de_image DE::pocketfft)
de_library(de_methods LAYER methods
  SOURCES methods/restoration_working.cpp methods/restoration_working_detail.hpp methods/restoration_working_values.cpp
          methods/restoration_working_mean.cpp methods/restoration_working_spectrum.cpp methods/restoration_working_map.cpp
          methods/working.cpp methods/gamma_working.cpp methods/levels_working.cpp methods/contrast_facts.cpp methods/sharpen_facts.cpp methods/nlm_facts.cpp methods/nlm_math.cpp methods/nlm_math.hpp methods/nlm_working.cpp methods/nlm_detail.hpp methods/nlm_prepare.cpp methods/nlm_storage.cpp methods/nlm_distances.cpp methods/nlm_accumulate.cpp methods/nlm_map.cpp methods/tvl1_facts.cpp methods/morphology_facts.cpp methods/illumination_fact_checks.cpp methods/surface_values.cpp methods/surface_facts.cpp methods/surface_fact_context.cpp methods/restoration_facts.cpp methods/restoration_fact_context.cpp methods/stage_reports.cpp methods/warnings.cpp methods/box_mean.cpp methods/catalog.cpp methods/binarization.cpp methods/binary_input.cpp methods/binary_detail.hpp methods/fixed_working.cpp methods/otsu_working.cpp methods/sauvola_working.cpp methods/sauvola_strip.cpp methods/sauvola_working_detail.hpp methods/wide_unsigned.cpp methods/wide_unsigned.hpp
          methods/restoration.cpp methods/restoration_psf.cpp methods/restoration_validation.cpp methods/restoration_resources.cpp methods/denoising.cpp methods/contrast.cpp methods/clahe_measure.cpp methods/clahe_apply.cpp methods/clahe_working.cpp methods/clahe_working_fit.cpp methods/contrast_detail.hpp methods/illumination.cpp methods/surface_detail.hpp methods/surface_grid.cpp
          methods/surface_system.cpp methods/surface_multigrid.cpp methods/surface_solve.cpp
          methods/surface_background.cpp methods/surface_measurement_math.cpp methods/surface_quantiles.cpp methods/surface_working.cpp methods/surface_working_cells.cpp methods/surface_working_fit.cpp methods/surface_working_samples.cpp methods/surface_working_map.cpp methods/surface_working_detail.hpp
          methods/illumination_apply.cpp methods/morphology_extrema.cpp methods/sharpening.cpp methods/sharpen_detail.hpp methods/unsharp_working.cpp methods/unsharp_working_fit.cpp
          methods/morphology_detail.hpp
          methods/morphology_coefficients.cpp methods/morphology_working.cpp methods/morphology_working_fit.cpp methods/morphology_working_samples.cpp methods/morphology_working_statistics.cpp methods/morphology_working_detail.hpp methods/tvl1_iteration.cpp methods/tvl1_detail.hpp methods/tvl1_solve.cpp methods/tvl1_working.cpp methods/tvl1_working_fit.cpp methods/tvl1_operators.hpp
          methods/tvl1_objective.cpp
  HEADERS "${DE_INCLUDE}/methods/box_mean.hpp" "${DE_INCLUDE}/methods/catalog.hpp"
          "${DE_INCLUDE}/methods/working.hpp" "${DE_INCLUDE}/methods/report_values.hpp"
          "${DE_INCLUDE}/methods/contrast_facts.hpp"
          "${DE_INCLUDE}/methods/sharpen_facts.hpp"
          "${DE_INCLUDE}/methods/binary_facts.hpp"
          "${DE_INCLUDE}/methods/nlm_facts.hpp" "${DE_INCLUDE}/methods/nlm_working.hpp"
          "${DE_INCLUDE}/methods/tvl1_facts.hpp"
          "${DE_INCLUDE}/methods/morphology_facts.hpp" "${DE_INCLUDE}/methods/surface_facts.hpp"
          "${DE_INCLUDE}/methods/restoration_facts.hpp" "${DE_INCLUDE}/methods/restoration_working.hpp" "${DE_INCLUDE}/methods/stage_reports.hpp" "${DE_INCLUDE}/methods/warnings.hpp"
          "${DE_INCLUDE}/methods/binarization.hpp" "${DE_INCLUDE}/methods/binarization_working.hpp"
          "${DE_INCLUDE}/methods/plan.hpp" "${DE_INCLUDE}/methods/method_catalog.hpp"
          "${DE_INCLUDE}/methods/method_parameter_metadata.hpp" "${DE_INCLUDE}/methods/parameters.hpp" "${DE_INCLUDE}/methods/parameter_fields.hpp" "${DE_INCLUDE}/methods/parameter_domains.hpp"
          "${DE_INCLUDE}/methods/restoration.hpp" "${DE_INCLUDE}/methods/denoising.hpp" "${DE_INCLUDE}/methods/sharpening.hpp" "${DE_INCLUDE}/methods/contrast.hpp" "${DE_INCLUDE}/methods/gamma.hpp" "${DE_INCLUDE}/methods/levels.hpp" "${DE_INCLUDE}/methods/clahe.hpp" "${DE_INCLUDE}/methods/unsharp.hpp" "${DE_INCLUDE}/methods/illumination.hpp" "${DE_INCLUDE}/methods/surface.hpp" "${DE_INCLUDE}/methods/surface_working.hpp" "${DE_INCLUDE}/methods/morphology.hpp" "${DE_INCLUDE}/methods/morphology_working.hpp" "${DE_INCLUDE}/methods/tvl1.hpp" "${DE_INCLUDE}/methods/tvl1_working.hpp"
  LINKS de_core de_exec de_image de_fft)
de_library(de_app LAYER app
  SOURCES app/response_validation.cpp app/admission.cpp app/admission.hpp app/plan.cpp app/plan.hpp app/parameter_bindings.hpp app/values.cpp app/dispatch.cpp app/verify.cpp app/process.cpp
  HEADERS "${DE_INCLUDE}/app/response_fields.hpp" "${DE_INCLUDE}/app/response.hpp" "${DE_INCLUDE}/app/dispatch.hpp" "${DE_INCLUDE}/app/process.hpp" "${DE_INCLUDE}/app/execution_control.hpp"
          "${DE_INCLUDE}/app/verify.hpp" "${DE_INCLUDE}/app/invocation.hpp"
          "${DE_INCLUDE}/app/values.hpp" "${DE_INCLUDE}/app/options.hpp"
  LINKS de_core de_methods de_image de_record)
target_include_directories(de_app PRIVATE "${PROJECT_BINARY_DIR}/generated")
de_library(de_record LAYER record
  SOURCES record/record.cpp record/inventory.cpp record/json.cpp record/request.cpp
          record/source_fields.cpp record/read_source.cpp record/read_claims.cpp
          record/read_observations.cpp record/validation.hpp
  HEADERS "${DE_INCLUDE}/record/record.hpp"
          "${DE_INCLUDE}/record/record_version.hpp" "${DE_INCLUDE}/record/inventory.hpp"
          "${DE_INCLUDE}/record/source.hpp" "${DE_INCLUDE}/record/source_fields.hpp"
          "${DE_INCLUDE}/record/request.hpp" "${DE_INCLUDE}/record/request_fields.hpp"
          "${DE_INCLUDE}/record/identity_fields.hpp" "${DE_INCLUDE}/record/method_requests.hpp"
          "${DE_INCLUDE}/record/method_request_fields.hpp" "${DE_INCLUDE}/record/json.hpp"
          "${DE_INCLUDE}/record/codec.hpp"
          "${DE_INCLUDE}/record/codec_read.hpp" "${DE_INCLUDE}/record/scalar.hpp"
          "${DE_INCLUDE}/record/scalar_read.hpp"
          "${DE_INCLUDE}/record/wire_type.hpp" "${DE_INCLUDE}/record/schema.hpp"
  LINKS de_core de_image de_methods PUBLIC_LINKS nlohmann_json::nlohmann_json)
de_library(de_report LAYER report
  SOURCES report/response_schema.cpp report/response.cpp report/warnings.cpp report/help.cpp report/help.hpp report/restoration.cpp report/restoration.hpp report/render.cpp report/continuous.cpp report/continuous.hpp report/illumination.cpp report/denoising.cpp report/sharpening.cpp report/sharpening.hpp report/contrast.cpp report/contrast.hpp report/denoising.hpp report/illumination.hpp HEADERS "${DE_INCLUDE}/report/response.hpp" "${DE_INCLUDE}/report/render.hpp" "${DE_INCLUDE}/report/warnings.hpp"
  LINKS de_app de_core de_image de_methods PUBLIC_LINKS de_record)
target_include_directories(de_report PRIVATE "${PROJECT_BINARY_DIR}/generated")
de_library(de_cli LAYER cli
  SOURCES cli/run.cpp cli/tokenize.cpp cli/failures.hpp HEADERS "${DE_INCLUDE}/cli/run.hpp" "${DE_INCLUDE}/cli/tokenize.hpp"
  LINKS de_app de_core de_report)

de_library(de_codec LAYER codec
  SOURCES codec/exif.cpp codec/exif.hpp codec/jpeg_context.cpp codec/jpeg_context.hpp
          codec/jpeg_decode.cpp codec/jpeg_markers.hpp codec/jpeg_memory.cpp codec/jpeg_metadata.cpp
          codec/jpeg_scan.cpp codec/jpeg_scan.hpp codec/png_artifact.cpp codec/png_chunks.cpp
          codec/png_chunks.hpp codec/png_context.cpp codec/png_context.hpp codec/encoded_bytes.hpp
          codec/png_binary.cpp codec/png_canonical.cpp codec/png_compare.cpp codec/png_memory_output.cpp codec/png_memory_output.hpp codec/png_metadata.cpp codec/png_metadata.hpp
          codec/png_profile.cpp codec/png_protection.cpp codec/png_psf.cpp codec/png_raster.cpp
          codec/png_rows.hpp
          codec/source.cpp codec/tiff_context.cpp codec/tiff_context.hpp codec/tiff_decode.cpp
          codec/tiff_ifd.cpp codec/tiff_ifd.hpp codec/tiff_layout.cpp codec/tiff_layout.hpp
          codec/tiff_pixels.cpp codec/tiff_tags.cpp
  HEADERS "${DE_INCLUDE}/codec/encoded_png.hpp" "${DE_INCLUDE}/codec/png_output.hpp" "${DE_INCLUDE}/codec/continuous_png.hpp" "${DE_INCLUDE}/codec/jpeg.hpp"
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
          fs/native_publication.hpp fs/publication.cpp fs/slot_identity.cpp
          fs/source_snapshot.cpp fs/windows_sdk.hpp
  HEADERS "${DE_INCLUDE}/fs/snapshot.hpp" "${DE_INCLUDE}/fs/artifact_limits.hpp"
          "${DE_INCLUDE}/fs/bundle_snapshot.hpp" "${DE_INCLUDE}/fs/digest.hpp"
          "${DE_INCLUDE}/fs/paths.hpp" "${DE_INCLUDE}/fs/publication.hpp"
  LINKS de_core DE::picosha2)
endif()
de_library(de_color LAYER color
  SOURCES color/context.cpp color/context.hpp color/profiles.cpp color/interpretation.cpp
          color/converter.cpp color/conversion.hpp color/prepare.cpp color/rows.cpp
          color/readers.cpp color/reader_pool.cpp
  HEADERS "${DE_INCLUDE}/color/interpretation.hpp" "${DE_INCLUDE}/color/profile.hpp"
  LINKS de_core de_image DE::lcms)
de_library(de_pipeline LAYER pipeline
  SOURCES pipeline/render.cpp pipeline/frame.cpp pipeline/frame.hpp pipeline/stages.cpp
          pipeline/assets.cpp pipeline/output.cpp pipeline/failure_context.hpp
  HEADERS "${DE_INCLUDE}/pipeline/render.hpp"
  LINKS de_core de_exec de_image de_methods de_codec de_color)
# Native schema content comes from the same typed writers and compiled catalogs.
add_executable(de_schema "${PROJECT_SOURCE_DIR}/tools/native_schema.cpp")
target_link_libraries(de_schema PRIVATE de_record de_report de_core nlohmann_json::nlohmann_json)
de_apply_options(de_schema)
if(DE_FUZZ_ONLY)
  target_compile_options(de_schema PRIVATE -fsanitize=fuzzer-no-link)
  target_link_options(de_schema PRIVATE -fsanitize=fuzzer-no-link)
endif()
de_register_architecture_client(de_schema "${PROJECT_SOURCE_DIR}/tools/native_schema.cpp")
add_custom_target(update-schemas
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/update_schemas.py" "$<TARGET_FILE:de_schema>"
  DEPENDS de_schema VERBATIM)
# Both fuzz groups compile production targets. Only codec harnesses link codec; the CLI closure
# remains pure and never composes the host or publication path.
if(DE_FUZZ_ONLY)
  de_write_architecture_targets("${PROJECT_BINARY_DIR}/architecture-targets.json" ROOTS cli codec color pipeline)
  return()
endif()
de_library(de_host LAYER host
  SOURCES host/processor.cpp host/context.cpp host/instant.cpp host/instant.hpp
          host/verifier.cpp host/bundle_verify.cpp host/bundle_verify.hpp
          host/run_publication.cpp host/run_publication.hpp
  HEADERS "${DE_INCLUDE}/host/processor.hpp" "${DE_INCLUDE}/host/verifier.hpp"
  LINKS de_core de_exec de_image de_methods de_app de_record de_fs de_codec de_pipeline de_color)
add_executable(docenhance entry/main.cpp entry/signals.cpp entry/signals.hpp)
if(MINGW)
  target_link_options(docenhance PRIVATE -municode)
endif()
target_link_libraries(docenhance PRIVATE de_cli de_core de_host)
de_apply_options(docenhance)
set_target_properties(docenhance PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${DE_BUILD_OWNER}/bin")
de_register_target(docenhance LAYER entry SOURCES entry/main.cpp entry/signals.cpp entry/signals.hpp LINKS de_cli de_core de_host)
de_write_architecture_targets("${PROJECT_BINARY_DIR}/architecture-targets.json")
