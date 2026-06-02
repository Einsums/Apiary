#----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
#----------------------------------------------------------------------------------------------

# ApiaryHelpers — CMake helper functions for projects that consume Apiary.
#
# Included automatically by ApiaryConfig.cmake (find_package(Apiary)) and by
# Apiary's own CMakeLists when used via add_subdirectory, so the same
# functions are available in both modes.
#
# P1 surface:
#   apiary_detect_toolchain([CXX_STANDARD <n>])
#       Probe the build compiler for the system/stdlib header search paths
#       libtooling needs to parse real C++ (resource-dir, conda isystem,
#       C++ standard-library dirs), caching the result in APIARY_* variables.
#
# Later phases add apiary_collect_usage_requirements(), apiary_add_bindings()
# and apiary_aggregate_extension().

include_guard(GLOBAL)

# Probe ``CMAKE_CXX_COMPILER`` (or a conda clang++) for the include paths
# libtooling needs and cache them as:
#   APIARY_RESOURCE_DIR        - clang -resource-dir (builtin headers)
#   APIARY_EXTRA_ISYSTEM       - active conda env's include dir (third-party)
#   APIARY_CXX_INCLUDE_DIRS    - the compiler's C++ stdlib search dirs
#
# Without these even ``#include <string>`` fails when the build compiler is
# gcc (libstdc++) rather than the clang that backs the apiary binary.
function(apiary_detect_toolchain)
    cmake_parse_arguments(_A "" "CXX_STANDARD" "" ${ARGN})
    if(NOT _A_CXX_STANDARD)
        set(_A_CXX_STANDARD 17)
    endif()

    # clang -resource-dir carries Clang's builtin headers (stddef.h, stdarg.h,
    # the x86 intrinsics, ...). Prefer the project compiler if it is clang,
    # else fall back to a conda clang++.
    set(_clang "")
    if(CMAKE_CXX_COMPILER MATCHES "clang")
        set(_clang "${CMAKE_CXX_COMPILER}")
    elseif(DEFINED ENV{CONDA_PREFIX} AND EXISTS "$ENV{CONDA_PREFIX}/bin/clang++")
        set(_clang "$ENV{CONDA_PREFIX}/bin/clang++")
    endif()
    set(_resource_dir "")
    if(_clang)
        execute_process(
            COMMAND "${_clang}" -print-resource-dir
            OUTPUT_VARIABLE _resource_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

    # Third-party headers (the consuming project's conda deps) live in the
    # active env include dir; mirror the project's ``-isystem ${CONDA}/include``.
    set(_extra_isystem "")
    if(DEFINED ENV{CONDA_PREFIX} AND EXISTS "$ENV{CONDA_PREFIX}/include")
        set(_extra_isystem "$ENV{CONDA_PREFIX}/include")
    endif()

    # The resource-dir does NOT carry the C++ standard library. Ask the real
    # project compiler for its ``#include <...>`` search list and forward the
    # relevant dirs as -isystem.
    #
    # Linux (gcc/icpx): forward ONLY the C++ library dirs (.../c++/...) — never
    # gcc's builtin dir (.../lib/gcc/.../include), whose intrinsics reference
    # gcc-only __builtin_ia32_* that Clang lacks.
    #
    # macOS: forward the WHOLE probed list (and rely on it instead of
    # -isysroot): the conda libc++ is newer than the SDK's, and mixing both
    # via -isysroot yields a dual-libc++ include_next collision.
    set(_cxx_dirs "")
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -std=c++${_A_CXX_STANDARD} -E -x c++ -v /dev/null
        OUTPUT_QUIET
        ERROR_VARIABLE _verbose
        RESULT_VARIABLE _rc
    )
    if(_rc EQUAL 0 AND _verbose)
        string(REGEX REPLACE "\r?\n" ";" _lines "${_verbose}")
        set(_in_search FALSE)
        foreach(_line IN LISTS _lines)
            if(_line MATCHES "#include <\\.\\.\\.> search starts here:")
                set(_in_search TRUE)
            elseif(_line MATCHES "End of search list\\.")
                set(_in_search FALSE)
            elseif(_in_search)
                string(STRIP "${_line}" _dir)
                if(_dir MATCHES " \\(framework directory\\)$")
                    continue()
                endif()
                if(NOT _dir OR NOT EXISTS "${_dir}")
                    continue()
                endif()
                if(APPLE)
                    list(APPEND _cxx_dirs "${_dir}")
                elseif(_dir MATCHES "/c\\+\\+")
                    list(APPEND _cxx_dirs "${_dir}")
                endif()
            endif()
        endforeach()
    endif()

    set(APIARY_RESOURCE_DIR     "${_resource_dir}"  CACHE INTERNAL "Clang -resource-dir for apiary codegen")
    set(APIARY_EXTRA_ISYSTEM    "${_extra_isystem}" CACHE INTERNAL "Extra -isystem dir for apiary codegen")
    set(APIARY_CXX_INCLUDE_DIRS "${_cxx_dirs}"      CACHE INTERNAL "C++ stdlib include dirs for apiary codegen")

    message(STATUS "apiary: resource-dir: ${APIARY_RESOURCE_DIR}")
    if(APIARY_EXTRA_ISYSTEM)
        message(STATUS "apiary: extra -isystem: ${APIARY_EXTRA_ISYSTEM}")
    endif()
    if(APIARY_CXX_INCLUDE_DIRS)
        message(STATUS "apiary: C++ include dirs: ${APIARY_CXX_INCLUDE_DIRS}")
    else()
        message(WARNING "apiary: could not probe ${CMAKE_CXX_COMPILER} for C++ "
            "stdlib include dirs — codegen may fail to find <complex> etc.")
    endif()

    # Assemble the ready-to-use system flag list consumers pass after ``--``.
    set(_flags "")
    if(APIARY_RESOURCE_DIR)
        list(APPEND _flags "-resource-dir" "${APIARY_RESOURCE_DIR}")
    endif()
    if(APIARY_EXTRA_ISYSTEM)
        list(APPEND _flags "-isystem" "${APIARY_EXTRA_ISYSTEM}")
    endif()
    foreach(_d IN LISTS APIARY_CXX_INCLUDE_DIRS)
        list(APPEND _flags "-isystem" "${_d}")
    endforeach()
    set(APIARY_SYSTEM_FLAGS "${_flags}" CACHE INTERNAL "Assembled -resource-dir/-isystem flags for apiary")
endfunction()
