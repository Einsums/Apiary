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

# Recursively collect BUILD_INTERFACE include directories AND
# INTERFACE_COMPILE_DEFINITIONS reachable from <target> via
# INTERFACE_LINK_LIBRARIES. CMake has no built-in for this; the walk is
# reliable since usage requirements are set explicitly via
# target_link_libraries / target_include_directories.
#
#   apiary_collect_usage_requirements(<target> INCLUDES_OUT <v> DEFINES_OUT <v>)
#
# Genex-wrapped entries are unwrapped for BUILD_INTERFACE and dropped
# otherwise (config-conditional values can't be evaluated at configure time).
function(apiary_collect_usage_requirements _target)
    cmake_parse_arguments(_A "" "INCLUDES_OUT;DEFINES_OUT" "" ${ARGN})
    set(_inc "")
    set(_def "")
    set(_visited "")
    _apiary_collect_impl("${_target}" _inc _def _visited)
    if(_inc)
        list(REMOVE_DUPLICATES _inc)
    endif()
    if(_def)
        list(REMOVE_DUPLICATES _def)
    endif()
    if(_A_INCLUDES_OUT)
        set(${_A_INCLUDES_OUT} "${_inc}" PARENT_SCOPE)
    endif()
    if(_A_DEFINES_OUT)
        set(${_A_DEFINES_OUT} "${_def}" PARENT_SCOPE)
    endif()
endfunction()

# Internal recursive worker. (Mirrors the parent-scope/local-copy dance the
# CMake function-scope model forces: PARENT_SCOPE updates the caller's var
# but not our inherited local, so we keep both in sync for recursion.)
function(_apiary_collect_impl _target inc_out def_out visited_var)
    if(NOT TARGET ${_target})
        return()
    endif()
    set(_local_inc     "${${inc_out}}")
    set(_local_def     "${${def_out}}")
    set(_local_visited "${${visited_var}}")
    if("${_target}" IN_LIST _local_visited)
        return()
    endif()
    list(APPEND _local_visited "${_target}")

    get_target_property(_i ${_target} INTERFACE_INCLUDE_DIRECTORIES)
    if(_i)
        foreach(_d IN LISTS _i)
            string(REGEX REPLACE "^\\$<BUILD_INTERFACE:(.+)>$" "\\1" _p "${_d}")
            if(NOT _p STREQUAL "${_d}")
                list(APPEND _local_inc "${_p}")
            elseif(NOT _d MATCHES "^\\$<")
                list(APPEND _local_inc "${_d}")
            endif()
        endforeach()
    endif()
    get_target_property(_c ${_target} INTERFACE_COMPILE_DEFINITIONS)
    if(_c)
        foreach(_d IN LISTS _c)
            string(REGEX REPLACE "^\\$<BUILD_INTERFACE:(.+)>$" "\\1" _p "${_d}")
            if(NOT _p STREQUAL "${_d}")
                list(APPEND _local_def "${_p}")
            elseif(NOT _d MATCHES "^\\$<")
                list(APPEND _local_def "${_d}")
            endif()
        endforeach()
    endif()
    set(${inc_out}     "${_local_inc}")
    set(${def_out}     "${_local_def}")
    set(${visited_var} "${_local_visited}")

    get_target_property(_link ${_target} INTERFACE_LINK_LIBRARIES)
    if(_link)
        foreach(_dep IN LISTS _link)
            if(TARGET ${_dep})
                _apiary_collect_impl("${_dep}" ${inc_out} ${def_out} ${visited_var})
                set(_local_inc     "${${inc_out}}")
                set(_local_def     "${${def_out}}")
                set(_local_visited "${${visited_var}}")
            endif()
        endforeach()
    endif()
    set(${inc_out}     "${_local_inc}"     PARENT_SCOPE)
    set(${def_out}     "${_local_def}"     PARENT_SCOPE)
    set(${visited_var} "${_local_visited}" PARENT_SCOPE)
endfunction()

# Emit the codegen custom command(s) that run apiary on one set of headers.
#
#   apiary_add_bindings(
#       HEADERS         <abs header...>      # parsed by apiary (positional)
#       SOURCE_INCLUDES <relative name...>   # --source-include each
#       REGISTER_FUNCTION <name>             # --register-function (binding TU)
#       MODULE          <name>               # --module (docs json; default "module")
#       DEPENDS_TARGETS <target...>          # usage requirements -> -I / -D
#       OUTPUT_DIR      <dir>                # where generated files land
#       OUTPUT_NAME     <stem>               # base filename for outputs
#       CXX_STANDARD    <n>                  # default 17
#       EXTRA_FLAGS     <flag...>            # extra -I/-D/... after --
#       EXTRA_DEPENDS   <file...>            # extra DEPENDS (e.g. Defines.hpp)
#       BINDING                              # emit the pybind TU (+ stub)
#       DOCS_JSON                            # emit the docs-json
#       OUT_BINDING <v>  OUT_STUB <v>  OUT_DOCS_JSON <v>
#   )
#
# Computes flags = APIARY_SYSTEM_FLAGS + usage-requirements(DEPENDS_TARGETS) +
# EXTRA_FLAGS + -std, and sets the requested OUT_* variables in the caller's
# scope. Requires apiary_detect_toolchain() to have run.
function(apiary_add_bindings)
    cmake_parse_arguments(_A "BINDING;DOCS_JSON"
        "REGISTER_FUNCTION;MODULE;OUTPUT_DIR;OUTPUT_NAME;CXX_STANDARD;OUT_BINDING;OUT_STUB;OUT_DOCS_JSON"
        "HEADERS;SOURCE_INCLUDES;DEPENDS_TARGETS;EXTRA_FLAGS;EXTRA_DEPENDS" ${ARGN})

    if(NOT _A_HEADERS)
        message(FATAL_ERROR "apiary_add_bindings: HEADERS is required")
    endif()
    if(NOT _A_OUTPUT_DIR)
        set(_A_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated/apiary")
    endif()
    if(NOT _A_OUTPUT_NAME)
        message(FATAL_ERROR "apiary_add_bindings: OUTPUT_NAME is required")
    endif()
    if(NOT _A_CXX_STANDARD)
        set(_A_CXX_STANDARD 17)
    endif()
    if(NOT _A_MODULE)
        set(_A_MODULE "module")
    endif()

    # -I from each dependency's transitive usage requirements; -D from their
    # compile definitions (so headers can gate INSTANTIATE choices on options).
    set(_inc_flags "")
    set(_def_flags "")
    foreach(_t IN LISTS _A_DEPENDS_TARGETS)
        apiary_collect_usage_requirements("${_t}" INCLUDES_OUT _i DEFINES_OUT _d)
        foreach(_dir IN LISTS _i)
            list(APPEND _inc_flags "-I${_dir}")
        endforeach()
        foreach(_def IN LISTS _d)
            list(APPEND _def_flags "-D${_def}")
        endforeach()
    endforeach()
    if(_inc_flags)
        list(REMOVE_DUPLICATES _inc_flags)
    endif()
    if(_def_flags)
        list(REMOVE_DUPLICATES _def_flags)
    endif()

    set(_source_includes "")
    foreach(_h IN LISTS _A_SOURCE_INCLUDES)
        list(APPEND _source_includes --source-include "${_h}")
    endforeach()

    # EXTRA_FLAGS before the dependency-derived -I so the consumer's explicit
    # include roots (e.g. the unit's own source dir) take search precedence
    # over those collected from its dependencies.
    set(_compile_flags
        -std=c++${_A_CXX_STANDARD}
        ${APIARY_SYSTEM_FLAGS}
        ${_def_flags}
        ${_A_EXTRA_FLAGS}
        ${_inc_flags}
    )

    if(_A_BINDING)
        set(_binding "${_A_OUTPUT_DIR}/${_A_OUTPUT_NAME}_pybind.cpp")
        set(_stub    "${_A_OUTPUT_DIR}/${_A_OUTPUT_NAME}.pyi")
        add_custom_command(
            OUTPUT ${_binding} ${_stub}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_A_OUTPUT_DIR}"
            COMMAND $<TARGET_FILE:apiary::apiary>
                    --register-function ${_A_REGISTER_FUNCTION}
                    --output ${_binding}
                    --stub-output ${_stub}
                    ${_source_includes}
                    ${_A_HEADERS}
                    -- ${_compile_flags}
            DEPENDS ${_A_HEADERS} apiary::apiary ${_A_EXTRA_DEPENDS}
            VERBATIM
            COMMENT "apiary: generating ${_A_OUTPUT_NAME}_pybind.cpp + .pyi"
        )
        if(_A_OUT_BINDING)
            set(${_A_OUT_BINDING} "${_binding}" PARENT_SCOPE)
        endif()
        if(_A_OUT_STUB)
            set(${_A_OUT_STUB} "${_stub}" PARENT_SCOPE)
        endif()
    endif()

    if(_A_DOCS_JSON)
        set(_docs "${_A_OUTPUT_DIR}/${_A_OUTPUT_NAME}.docs.json")
        add_custom_command(
            OUTPUT ${_docs}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_A_OUTPUT_DIR}"
            COMMAND $<TARGET_FILE:apiary::apiary>
                    --emit-docs-json
                    --module ${_A_MODULE}
                    --output ${_docs}
                    ${_source_includes}
                    ${_A_HEADERS}
                    -- ${_compile_flags}
            DEPENDS ${_A_HEADERS} apiary::apiary ${_A_EXTRA_DEPENDS}
            VERBATIM
            COMMENT "apiary: emitting docs JSON for ${_A_OUTPUT_NAME}"
        )
        if(_A_OUT_DOCS_JSON)
            set(${_A_OUT_DOCS_JSON} "${_docs}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

# Assemble per-module bindings into one Python extension.
#
#   apiary_aggregate_extension(
#       NAME <target>                # pybind11 module target to create
#       MAIN <main.cpp>              # consumer's PYBIND11_MODULE source
#       BINDINGS <generated tu...>   # per-module binding TUs (from apiary_add_bindings)
#       MODULES <name...>            # modules to register
#       REGISTER_PREFIX <p>          # default apiary_register_
#       MODULES_HEADER <path>        # generated header declaring <prefix>all()
#       MODULES_INCLUDE_DIR <dir>    # added to the target so MAIN finds the header
#       # optional .pyi aggregation:
#       STUBS <pyi...>  STUBS_TARGET <name>  FRAG_DIR <d>  PKG_DIR <d>
#       PY_HELPERS_DIR <d>  PY_HELPER_DEPENDS <file...>
#       # optional docs render (built on demand, not part of ALL):
#       DOCS_JSON <json...>  DOCS_TARGET <name>  DOCS_OUTDIR <d>
#   )
#
# Generates the register header, creates the pybind11 module (the consumer
# configures output name / linkage / packaging afterward), and — when the
# optional groups are given — wires the stub-aggregation (ALL) and docs-render
# (on-demand) targets using Apiary's bundled scripts (APIARY_SCRIPTS_DIR).
function(apiary_aggregate_extension)
    cmake_parse_arguments(_A ""
        "NAME;MAIN;REGISTER_PREFIX;MODULES_HEADER;MODULES_INCLUDE_DIR;STUBS_TARGET;FRAG_DIR;PKG_DIR;PY_HELPERS_DIR;DOCS_TARGET;DOCS_OUTDIR"
        "MODULES;BINDINGS;STUBS;PY_HELPER_DEPENDS;DOCS_JSON" ${ARGN})

    foreach(_req NAME MAIN MODULES_HEADER)
        if(NOT _A_${_req})
            message(FATAL_ERROR "apiary_aggregate_extension: ${_req} is required")
        endif()
    endforeach()
    if(NOT _A_REGISTER_PREFIX)
        set(_A_REGISTER_PREFIX "apiary_register_")
    endif()

    # 1. The per-module register declarations + inline <prefix>all() aggregator
    #    the MAIN includes. The only piece that varies with the module set.
    set(_protos "")
    set(_calls "")
    foreach(_m IN LISTS _A_MODULES)
        string(APPEND _protos "void ${_A_REGISTER_PREFIX}${_m}(::pybind11::module_ &m);\n")
        string(APPEND _calls  "    ${_A_REGISTER_PREFIX}${_m}(m);\n")
    endforeach()
    get_filename_component(_hdr_dir "${_A_MODULES_HEADER}" DIRECTORY)
    file(MAKE_DIRECTORY "${_hdr_dir}")
    file(WRITE "${_A_MODULES_HEADER}"
        "// Generated by apiary_aggregate_extension. Do not edit.\n"
        "//\n"
        "// Per-module register-function declarations and the inline aggregator\n"
        "// called from the extension's PYBIND11_MODULE body.\n"
        "\n"
        "#pragma once\n"
        "\n"
        "#include <pybind11/pybind11.h>\n"
        "\n"
        "${_protos}"
        "\n"
        "inline void ${_A_REGISTER_PREFIX}all(::pybind11::module_ &m) {\n"
        "${_calls}"
        "}\n"
    )

    # 2. The extension target. Consumer configures OUTPUT_NAME / output dir /
    #    linkage / packaging after this call.
    pybind11_add_module(${_A_NAME} "${_A_MAIN}" ${_A_BINDINGS})
    if(_A_MODULES_INCLUDE_DIR)
        target_include_directories(${_A_NAME} PRIVATE "${_A_MODULES_INCLUDE_DIR}")
    endif()

    # The stub/docs steps run bundled Python scripts; make sure we have an
    # interpreter even if the consumer didn't already find one.
    if((_A_STUBS_TARGET OR _A_DOCS_TARGET) AND NOT Python_EXECUTABLE)
        find_package(Python COMPONENTS Interpreter REQUIRED)
    endif()

    # 3. .pyi aggregation (ALL) — optional.
    if(_A_STUBS_TARGET)
        set(_stamp "${_A_FRAG_DIR}/.stubs.stamp")
        add_custom_command(
            OUTPUT ${_stamp}
            COMMAND ${Python_EXECUTABLE} "${APIARY_SCRIPTS_DIR}/aggregate_stubs.py"
                    --frag-dir "${_A_FRAG_DIR}"
                    --pkg-dir "${_A_PKG_DIR}"
                    --py-helpers-dir "${_A_PY_HELPERS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E touch ${_stamp}
            DEPENDS "${APIARY_SCRIPTS_DIR}/aggregate_stubs.py" ${_A_STUBS} ${_A_PY_HELPER_DEPENDS}
            COMMENT "apiary: aggregating .pyi stubs into ${_A_PKG_DIR}"
            VERBATIM
        )
        add_custom_target(${_A_STUBS_TARGET} ALL DEPENDS ${_stamp})
        add_dependencies(${_A_STUBS_TARGET} ${_A_NAME})
    endif()

    # 4. Docs render (on demand, NOT part of ALL) — optional.
    if(_A_DOCS_TARGET)
        set(_dstamp "${_A_DOCS_OUTDIR}/.docs.stamp")
        add_custom_command(
            OUTPUT ${_dstamp}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_A_DOCS_OUTDIR}"
            COMMAND ${Python_EXECUTABLE} "${APIARY_SCRIPTS_DIR}/render_docs_rst.py"
                    --outdir "${_A_DOCS_OUTDIR}" ${_A_DOCS_JSON}
            COMMAND ${CMAKE_COMMAND} -E touch ${_dstamp}
            DEPENDS "${APIARY_SCRIPTS_DIR}/render_docs_rst.py" ${_A_DOCS_JSON}
            COMMENT "apiary: rendering Python API reference into ${_A_DOCS_OUTDIR}"
            VERBATIM
        )
        add_custom_target(${_A_DOCS_TARGET} DEPENDS ${_dstamp})
    endif()
endfunction()
