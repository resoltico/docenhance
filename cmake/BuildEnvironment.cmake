# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
# Compiler search/flag injection is not represented by the admitted compilation command.
foreach(name IN ITEMS CFLAGS CXXFLAGS CPPFLAGS LDFLAGS CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH
    OBJC_INCLUDE_PATH LIBRARY_PATH COMPILER_PATH GCC_EXEC_PREFIX CL _CL_ LINK _LINK_ CCC_OVERRIDE_OPTIONS)
  if(DEFINED ENV{${name}})
    if(NOT "$ENV{${name}}" STREQUAL "")
      message(FATAL_ERROR "${name} is unsupported: unset it and use the reviewed build options")
    endif()
  endif()
endforeach()
