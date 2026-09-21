# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
include(GNUInstallDirs)
set(de_package_meta "${PROJECT_BINARY_DIR}/package-metadata")
add_custom_target(de_license_inventory ALL
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/license_inventory.py"
    --cache "${DE_SOURCE_CACHE}" --out "${de_package_meta}"
    --platform "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
    --compiler "${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
  VERBATIM)
add_dependencies(docenhance de_license_inventory)
install(TARGETS docenhance RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/README.md" DESTINATION .)
install(DIRECTORY "${de_package_meta}/" DESTINATION share/docenhance)
install(FILES "${PROJECT_SOURCE_DIR}/spec/cli-contract.json" "${PROJECT_SOURCE_DIR}/spec/method-contract.json"
  DESTINATION share/docenhance/spec)
include(PackageMetadata)
include(CPack)
