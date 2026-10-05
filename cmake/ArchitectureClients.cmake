# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
include_guard(GLOBAL)
# Registered clients are outside production layers. Their root permissions still come from the
# architecture manifest, while generator-time target properties establish the actual direct links.
function(de_register_architecture_client target source)
  file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${source}")
  string(JSON root ERROR_VARIABLE absent GET "${DE_ARCHITECTURE_JSON}" clients "${relative}")
  if(absent)
    return()
  endif()
  file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/architecture-client-${target}.json"
    CONTENT "{\"target\":\"${target}\",\"source\":\"${relative}\",\"links\":\"$<JOIN:$<TARGET_PROPERTY:${target},LINK_LIBRARIES>,;>\"}\n")
endfunction()
