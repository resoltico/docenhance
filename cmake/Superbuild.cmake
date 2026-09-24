# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
include(ExternalProject)
# Explicit source order; serial projects avoid N libraries each starting N workers.
if(DE_FUZZ_ONLY)
  set(de_names zlib png lcms cli11 json)
else()
  set(de_names zlib jpeg png tiff opencv leptonica lcms cli11 json picosha2)
  if(DE_BUILD_TESTS)
    list(APPEND de_names catch2)
  endif()
endif()
# Fail before creating dependency builds. Verification never opens a network connection.
set(de_verify_commands)
foreach(name IN LISTS de_names)
  set(de_verify "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/deps.py" verify
    --cache "${DE_SOURCE_CACHE}" --dependency ${name})
  execute_process(COMMAND ${de_verify} COMMAND_ERROR_IS_FATAL ANY)
  list(APPEND de_verify_commands COMMAND ${de_verify})
endforeach()
file(READ "${PROJECT_SOURCE_DIR}/deps/features.json" de_features)
set(de_previous "")
set(de_common
  "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
  "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
  "-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
  "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}"
  "-DCMAKE_INSTALL_PREFIX:PATH=${DE_DEPENDENCY_PREFIX}"
  "-DCMAKE_INSTALL_LIBDIR:STRING=lib"
  "-DCMAKE_INSTALL_INCLUDEDIR:STRING=include"
  "-DCMAKE_PREFIX_PATH:PATH=${DE_DEPENDENCY_PREFIX}"
  "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"
  "-DCMAKE_FIND_USE_PACKAGE_REGISTRY:BOOL=OFF"
  "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY:BOOL=OFF"
  "-DBUILD_SHARED_LIBS:BOOL=OFF"
  "-DBUILD_TESTING:BOOL=OFF"
  "-DCMAKE_POLICY_DEFAULT_CMP0091:STRING=NEW"
  "-DCMAKE_MSVC_RUNTIME_LIBRARY:STRING=${CMAKE_MSVC_RUNTIME_LIBRARY}")
if(APPLE)
  list(APPEND de_common "-DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=${CMAKE_OSX_DEPLOYMENT_TARGET}"
    "-DCMAKE_OSX_ARCHITECTURES:STRING=${CMAKE_OSX_ARCHITECTURES}")
endif()
if(CMAKE_TOOLCHAIN_FILE)
  list(APPEND de_common "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}")
endif()
foreach(name IN LISTS de_names)
  set(de_source "${DE_SOURCE_CACHE}/sources/${name}")
  set(de_binary "${PROJECT_BINARY_DIR}/deps/${name}")
  set(de_options)
  string(JSON de_count LENGTH "${de_features}" dependencies "${name}")
  if(de_count GREATER 0)
    math(EXPR de_last "${de_count} - 1")
    foreach(i RANGE 0 ${de_last})
      string(JSON key MEMBER "${de_features}" dependencies "${name}" ${i})
      string(JSON value GET "${de_features}" dependencies "${name}" "${key}")
      string(JSON kind TYPE "${de_features}" dependencies "${name}" "${key}")
      string(REPLACE "<BINARY>" "${de_binary}" value "${value}")
      if(kind STREQUAL "BOOLEAN")
        list(APPEND de_options "-D${key}:BOOL=${value}")
      else()
        list(APPEND de_options "-D${key}:STRING=${value}")
      endif()
    endforeach()
  endif()
  if(name STREQUAL "picosha2")
    set(de_source "${PROJECT_SOURCE_DIR}/cmake/dependencies/picosha2")
    list(APPEND de_options "-DDE_UPSTREAM_SOURCE:PATH=${DE_SOURCE_CACHE}/sources/picosha2")
  endif()
  if(name STREQUAL "opencv")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
      list(APPEND de_options "-DCPU_BASELINE:STRING=SSE2")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
      list(APPEND de_options "-DCPU_BASELINE:STRING=NEON")
    else()
      message(FATAL_ERROR "Release baseline is defined only for x86-64 and ARM64")
    endif()
    # OpenCV must not locate Python package wrappers, codecs or neural backends.
    list(APPEND de_options "-DOPENCV_FORCE_PYTHON_LIBS:BOOL=OFF" "-DOPENCV_CMAKE_HOOKS_DIR:PATH=${PROJECT_SOURCE_DIR}/cmake/opencv-hooks" "-DZLIB_ROOT:PATH=${DE_DEPENDENCY_PREFIX}")
  endif()
  if(name STREQUAL "png" OR name STREQUAL "tiff")
    list(APPEND de_options "-DZLIB_ROOT:PATH=${DE_DEPENDENCY_PREFIX}")
  endif()
  if(WIN32 AND (name STREQUAL "opencv" OR name STREQUAL "png" OR name STREQUAL "tiff"))
    # zlib names its static Windows archive zs.lib; CMake's FindZLIB does not probe that name.
    # This remains a configuration-private path to the zlib ExternalProject, never a host lookup.
    list(APPEND de_options "-DZLIB_LIBRARY:FILEPATH=${DE_DEPENDENCY_PREFIX}/lib/zs.lib")
  endif()
  if(WIN32 AND name STREQUAL "opencv")
    # OpenCV 5's MinGW compatibility path otherwise exports a bare pthread.lib when CMake
    # configures it with the current MSVC-compatible runner. There is no pinned pthread
    # runtime in this source-only build, so explicitly use OpenCV's serial Windows path.
    list(APPEND de_options "-DOPENCV_DISABLE_THREAD_SUPPORT:BOOL=ON")
  endif()
  if(WIN32 AND name STREQUAL "leptonica")
    # Leptonica's setPixMemoryManager() is compiled out under MSVC unless all allocation calls
    # are intercepted. The native probe provides the reviewed allocator symbols explicitly.
    list(APPEND de_options "-DCMAKE_C_FLAGS:STRING=/DLEPTONICA_INTERCEPT_ALLOC")
  endif()
  if(name STREQUAL "tiff")
    list(APPEND de_options "-DJPEG_ROOT:PATH=${DE_DEPENDENCY_PREFIX}")
  endif()
  if(DE_FUZZ_ONLY AND (name STREQUAL "png" OR name STREQUAL "zlib" OR name STREQUAL "lcms"))
    # Instrument the actual pinned C decoder/decompressor, not just the C++ adapter.
    list(APPEND de_options
      "-DCMAKE_C_FLAGS:STRING=-fsanitize=address,undefined,fuzzer-no-link -fno-sanitize-recover=all -fno-omit-frame-pointer")
  endif()
  ExternalProject_Add(de_dep_${name}
    SOURCE_DIR "${de_source}" BINARY_DIR "${de_binary}"
    PREFIX "${PROJECT_BINARY_DIR}/ep/${name}"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND "" PATCH_COMMAND ""
    CMAKE_GENERATOR Ninja CMAKE_ARGS ${de_common} ${de_options}
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${DE_BUILD_JOBS}"
    INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR>
    TEST_COMMAND ""
    DEPENDS ${de_previous}
    USES_TERMINAL_CONFIGURE TRUE USES_TERMINAL_BUILD TRUE USES_TERMINAL_INSTALL TRUE)
  set(de_previous de_dep_${name})
endforeach()
set(de_inner "${PROJECT_BINARY_DIR}/app")
# Fuzz runs are CTest tests too, even when the unit tests are not built.
if(DE_BUILD_TESTS OR DE_ENABLE_FUZZING)
  set(de_inner_testing ON)
else()
  set(de_inner_testing OFF)
endif()
ExternalProject_Add(de_native
  SOURCE_DIR "${PROJECT_SOURCE_DIR}" BINARY_DIR "${de_inner}"
  PREFIX "${PROJECT_BINARY_DIR}/ep/application"
  DOWNLOAD_COMMAND "" UPDATE_COMMAND "" PATCH_COMMAND ""
  CMAKE_GENERATOR Ninja
  CMAKE_ARGS ${de_common}
    "-DDE_SUPERBUILD:BOOL=OFF"
    "-DBUILD_TESTING:BOOL=${de_inner_testing}"
    "-DDE_DEPENDENCY_PREFIX:PATH=${DE_DEPENDENCY_PREFIX}"
    "-DDE_SOURCE_CACHE:PATH=${DE_SOURCE_CACHE}"
    "-DDE_SUPERBUILD_BINARY:PATH=${PROJECT_BINARY_DIR}"
    "-DDE_BUILD_TESTS:BOOL=${DE_BUILD_TESTS}"
    "-DDE_BUILD_TOOLS:BOOL=${DE_BUILD_TOOLS}"
    "-DDE_WARNINGS_AS_ERRORS:BOOL=${DE_WARNINGS_AS_ERRORS}"
    "-DDE_ENABLE_ASAN:BOOL=${DE_ENABLE_ASAN}"
    "-DDE_ENABLE_UBSAN:BOOL=${DE_ENABLE_UBSAN}"
    "-DDE_ENABLE_TSAN:BOOL=${DE_ENABLE_TSAN}"
    "-DDE_ENABLE_IPO:BOOL=${DE_ENABLE_IPO}"
    "-DDE_ENABLE_CLANG_TIDY:BOOL=${DE_ENABLE_CLANG_TIDY}"
    "-DDE_ENABLE_FUZZING:BOOL=${DE_ENABLE_FUZZING}"
    "-DDE_FUZZ_ONLY:BOOL=${DE_FUZZ_ONLY}"
    "-DDE_FUZZ_SECONDS:STRING=${DE_FUZZ_SECONDS}"
    "-DDE_FUZZ_ENGINE:STRING=${DE_FUZZ_ENGINE}"
    "-DDE_FUZZ_JOBS:STRING=${DE_FUZZ_JOBS}"
    "-DDE_BUILD_JOBS:STRING=${DE_BUILD_JOBS}"
    "-DPython3_EXECUTABLE:FILEPATH=${Python3_EXECUTABLE}"
  BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${DE_BUILD_JOBS}"
  BUILD_ALWAYS TRUE INSTALL_COMMAND "" TEST_COMMAND ""
  DEPENDS ${de_previous}
  USES_TERMINAL_CONFIGURE TRUE USES_TERMINAL_BUILD TRUE)
# The audit cannot be skipped merely because ExternalProject's configure stamps exist.
add_custom_target(de_verify_sources ALL ${de_verify_commands} VERBATIM)
list(GET de_names 0 de_first)
add_dependencies(de_dep_${de_first} de_verify_sources)
if(DE_BUILD_TESTS)
  add_test(NAME native-suite COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${de_inner}" --output-on-failure --no-tests=error)
  set_tests_properties(native-suite PROPERTIES TIMEOUT 180)
endif()
if(DE_ENABLE_FUZZING)
  add_test(NAME fuzz-suite COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/run_fuzz_campaign.py"
    --build "${de_inner}" --seconds "${DE_FUZZ_SECONDS}" --jobs "${DE_FUZZ_JOBS}"
    --ctest "${CMAKE_CTEST_COMMAND}")
  math(EXPR de_fuzz_timeout "${DE_FUZZ_CAMPAIGN_TIMEOUT} + 30")
  set_tests_properties(fuzz-suite PROPERTIES TIMEOUT ${de_fuzz_timeout})
endif()
foreach(gate IN ITEMS check-project check-spec check-architecture check-gates check-format)
  add_custom_target(${gate}
    COMMAND "${CMAKE_COMMAND}" --build "${de_inner}" --target ${gate}
    DEPENDS de_native USES_TERMINAL VERBATIM)
endforeach()
set(CPACK_INSTALL_CMAKE_PROJECTS "${de_inner};DocEnhance;ALL;/")
include(PackageMetadata)
include(CPack)
