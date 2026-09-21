# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 4.4)
get_filename_component(DE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
if(NOT DE_SOURCE_CACHE)
  set(DE_SOURCE_CACHE "${DE_ROOT}/.cache/deps")
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${DE_ROOT}/tools/deps.py" fetch
  --cache "${DE_SOURCE_CACHE}" COMMAND_ERROR_IS_FATAL ANY)
