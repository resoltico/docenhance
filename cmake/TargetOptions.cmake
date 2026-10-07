# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
include_guard(GLOBAL)

# Read final target properties, including overrides made after de_apply_options().
# Source comments and disabled CMake branches cannot establish compiler/linter admission.
function(de_verify_target_options directory)
  get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(kind ${target} TYPE)
    if(NOT kind MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
      continue()
    endif()
    get_target_property(links ${target} LINK_LIBRARIES)
    if(NOT "DocEnhance::options" IN_LIST links)
      message(FATAL_ERROR "${target} lacks the actual DocEnhance::options link")
    endif()
    get_target_property(source_directory ${target} SOURCE_DIR)
    get_target_property(sources ${target} SOURCES)
    foreach(source IN LISTS sources)
      # Source-property keys retain CMake directory spelling, including DOS short names.
      cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${source_directory}"
        NORMALIZE OUTPUT_VARIABLE source_path)
      get_source_file_property(skip "${source_path}" TARGET_DIRECTORY ${target} SKIP_LINTING)
      if(skip)
        message(FATAL_ERROR "${target} source ${source} has effective SKIP_LINTING")
      endif()
    endforeach()
    if(DE_ENABLE_CLANG_TIDY)
      get_target_property(tidy ${target} CXX_CLANG_TIDY)
      if(NOT tidy STREQUAL DE_CLANG_TIDY_COMMAND)
        message(FATAL_ERROR "${target} effective CXX_CLANG_TIDY differs from the required command")
      endif()
    endif()
  endforeach()
  get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(child IN LISTS children)
    de_verify_target_options("${child}")
  endforeach()
endfunction()
cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}"
  CALL de_verify_target_options "${PROJECT_SOURCE_DIR}")
