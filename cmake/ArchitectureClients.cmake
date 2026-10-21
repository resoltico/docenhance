# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
include_guard(GLOBAL)
# Registered clients are outside production layers. Their root permissions still come from the
# architecture manifest; final raw target properties establish their actual direct links.
function(de_register_architecture_client target source)
  file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${source}")
  string(JSON root ERROR_VARIABLE absent GET "${DE_ARCHITECTURE_JSON}" clients "${relative}")
  if(absent)
    return()
  endif()
  set_property(GLOBAL APPEND PROPERTY DE_ARCHITECTURE_CLIENT_TARGETS "${target}")
  set_property(TARGET ${target} PROPERTY DE_ARCHITECTURE_CLIENT_SOURCE "${relative}")
endfunction()

function(de_write_architecture_clients)
  get_property(targets GLOBAL PROPERTY DE_ARCHITECTURE_CLIENT_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(relative ${target} DE_ARCHITECTURE_CLIENT_SOURCE)
    # TARGET_PROPERTY's LINK_LIBRARIES genex is transitive under CMP0189. Read the final raw
    # property instead, so native usage requirements do not masquerade as direct client links.
    get_property(links TARGET ${target} PROPERTY LINK_LIBRARIES)
    file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/architecture-client-${target}.json"
      CONTENT "{\"target\":\"${target}\",\"source\":\"${relative}\",\"links\":\"$<JOIN:${links},;>\"}\n"
      TARGET ${target})
  endforeach()
endfunction()
cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}" CALL de_write_architecture_clients)
