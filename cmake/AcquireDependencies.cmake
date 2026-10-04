# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 4.4)
get_filename_component(DE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(READ "${DE_ROOT}/deps/tools.json" de_tools_json)
string(JSON de_python_minimum GET "${de_tools_json}" python minimum)
find_package(Python3 ${de_python_minimum} REQUIRED COMPONENTS Interpreter)
if(NOT DE_SOURCE_CACHE)
  set(DE_SOURCE_CACHE "${DE_ROOT}/.cache/deps")
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${DE_ROOT}/tools/deps.py" fetch
  --cache "${DE_SOURCE_CACHE}" COMMAND_ERROR_IS_FATAL ANY)
