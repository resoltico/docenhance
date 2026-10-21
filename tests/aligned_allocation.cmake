# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
# Isolated scalar aligned-nothrow replacement; no ordinary/C allocation observer is linked.
add_executable(de_core_aligned_null support/core_aligned_null.cpp)
target_link_libraries(de_core_aligned_null PRIVATE de_core)
de_apply_options(de_core_aligned_null)
add_test(NAME core-aligned-null-refund COMMAND de_core_aligned_null)
