# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
include_guard(GLOBAL)
function(de_require_owned_dependency_path path role)
  file(REAL_PATH "${DE_DEPENDENCY_PREFIX}" prefix)
  file(REAL_PATH "${path}" resolved)
  cmake_path(IS_PREFIX prefix "${resolved}" NORMALIZE owned)
  if(NOT owned OR NOT EXISTS "${resolved}")
    message(FATAL_ERROR "${role} is outside the owned dependency prefix: ${path}")
  endif()
endfunction()
function(de_validate_package_directory package)
  set(name "${package}_DIR")
  if(DEFINED ${name} AND NOT "${${name}}" MATCHES "-NOTFOUND$" AND NOT "${${name}}" STREQUAL "")
    # Validate before find_package can execute a cached foreign configuration.
    de_require_owned_dependency_path("${${name}}" "${name}")
  endif()
endfunction()
