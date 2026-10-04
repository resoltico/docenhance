# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
include(GNUInstallDirs)
set(de_package_meta "${PROJECT_BINARY_DIR}/package-metadata")
add_custom_target(de_license_inventory ALL
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/license_inventory.py"
    --cache "${DE_SOURCE_CACHE}" --out "${de_package_meta}"
    --platform "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
    --compiler "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
  VERBATIM)
add_dependencies(docenhance de_license_inventory)
install(TARGETS docenhance RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION .)
install(FILES "${PROJECT_SOURCE_DIR}/README.md" "${PROJECT_SOURCE_DIR}/LICENSE"
  "${PROJECT_SOURCE_DIR}/AGENTS.md" "${PROJECT_SOURCE_DIR}/CONTRIBUTING.md"
  "${PROJECT_SOURCE_DIR}/CHANGELOG.md" DESTINATION share/docenhance)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/" DESTINATION share/docenhance/docs FILES_MATCHING PATTERN "*.md")
install(FILES "${PROJECT_SOURCE_DIR}/fuzz/README.md" DESTINATION share/docenhance/fuzz)
install(FILES "${PROJECT_SOURCE_DIR}/.github/SECURITY.md" "${PROJECT_SOURCE_DIR}/.github/CODE_OF_CONDUCT.md"
  DESTINATION share/docenhance/.github)
install(DIRECTORY "${de_package_meta}/" DESTINATION share/docenhance)
install(FILES "${PROJECT_SOURCE_DIR}/spec/cli-contract.json" "${PROJECT_SOURCE_DIR}/spec/method-contract.json"
  DESTINATION share/docenhance/spec)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/schemas/" DESTINATION share/docenhance/schemas)
include(PackageMetadata)
include(CPack)
