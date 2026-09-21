# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 4.4)
project(DocEnhancePicoSHA2 LANGUAGES NONE)
if(NOT EXISTS "${DE_UPSTREAM_SOURCE}/picosha2.h")
  message(FATAL_ERROR "Verified PicoSHA2 source required")
endif()
include(GNUInstallDirs)
install(FILES "${DE_UPSTREAM_SOURCE}/picosha2.h" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
