#----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
#----------------------------------------------------------------------------------------------

# The two install components, each on its own and both together.
#
# A package that ships the annotations apart from the tool installs one
# component per package, so the two must not share a file, an install of
# either must be what find_package(Apiary) expects, and the pair must add up
# to a plain install. Each prefix is checked by configuring a small consumer
# project against it, since find_package and imported targets need a project.
#
# Invocation:
#     cmake -DBUILD_DIR=<apiary build> -DWORK_DIR=<scratch> -DTOOL=<ON|OFF>
#           -DGENERATOR=<generator> -P run_install_components.cmake

foreach(_var BUILD_DIR WORK_DIR TOOL GENERATOR)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "run_install_components: ${_var} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

function(_install prefix)
    set(_args)
    if(ARGN)
        set(_args --component ${ARGN})
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix}" ${_args}
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _out
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "install of '${ARGN}' into ${prefix} failed:\n${_out}")
    endif()
endfunction()

# Every file under <prefix>, relative to it, sorted.
function(_list_files prefix out_var)
    file(GLOB_RECURSE _files LIST_DIRECTORIES false RELATIVE "${prefix}" "${prefix}/*")
    list(SORT _files)
    set(${out_var} "${_files}" PARENT_SCOPE)
endfunction()

_install("${WORK_DIR}/annotations" ApiaryAnnotations)
_install("${WORK_DIR}/tool" ApiaryTool)
_install("${WORK_DIR}/both" ApiaryAnnotations)
_install("${WORK_DIR}/both" ApiaryTool)
_install("${WORK_DIR}/full")

_list_files("${WORK_DIR}/annotations" _annotations)
_list_files("${WORK_DIR}/tool" _tool)
_list_files("${WORK_DIR}/both" _both)
_list_files("${WORK_DIR}/full" _full)

# Nothing outside the two components: a file in neither would be in "full"
# and missing from "both".
if(NOT _both STREQUAL _full)
    message(FATAL_ERROR "the two components do not add up to a full install\n"
                        "  components: ${_both}\n  full:       ${_full}")
endif()

set(_shared)
foreach(_f IN LISTS _annotations)
    if(_f IN_LIST _tool)
        list(APPEND _shared "${_f}")
    endif()
endforeach()
if(_shared)
    message(FATAL_ERROR "files in both components: ${_shared}")
endif()

# The annotations component must need nothing from the tool's: no executable,
# no helpers, no scripts.
foreach(_f IN LISTS _annotations)
    if(_f MATCHES "(^|/)bin/|ApiaryTool|ApiaryHelpers|ApiaryRun|/scripts/")
        message(FATAL_ERROR "the annotations component installs a tool file: ${_f}")
    endif()
endforeach()

# Configures a consumer against the Apiary installed in <prefix>, asking
# find_package(Apiary) for <components>, and checks what it gets. <expect> is
# NOT_FOUND, or what was found: ANNOTATIONS (nothing of the tool), HELPERS (the
# helpers and scripts a build without the tool installs) or TOOL (those and
# the executable). Apiary_DIR is pinned, so an Apiary installed in the active
# environment cannot stand in for the one under test.
function(_consume name prefix components expect)
    file(GLOB_RECURSE _config "${prefix}/*/ApiaryConfig.cmake")
    if(NOT _config)
        message(FATAL_ERROR "no ApiaryConfig.cmake under ${prefix}")
    endif()
    get_filename_component(_apiary_dir "${_config}" DIRECTORY)
    set(_src "${WORK_DIR}/consumer-${name}")
    file(MAKE_DIRECTORY "${_src}")
    file(WRITE "${_src}/CMakeLists.txt" "
cmake_minimum_required(VERSION 3.20)
project(consumer LANGUAGES NONE)
find_package(Apiary CONFIG COMPONENTS ${components})
set(expect ${expect})
if(expect STREQUAL NOT_FOUND)
    if(Apiary_FOUND)
        message(FATAL_ERROR \"Apiary was found with COMPONENTS ${components}\")
    endif()
    if(NOT Apiary_NOT_FOUND_MESSAGE MATCHES \"ApiaryTool\")
        message(FATAL_ERROR \"no reason given for the missing tool: '\${Apiary_NOT_FOUND_MESSAGE}'\")
    endif()
    # A failed find_package must define nothing, so that a fallback building
    # Apiary from source does not collide with the targets of the one refused.
    if(TARGET apiary::annotations OR TARGET apiary::apiary OR COMMAND apiary_add_bindings)
        message(FATAL_ERROR \"the refused install left its targets or helpers behind\")
    endif()
    return()
endif()
if(NOT Apiary_FOUND)
    message(FATAL_ERROR \"Apiary was not found with COMPONENTS ${components}\")
endif()
if(NOT TARGET apiary::annotations)
    message(FATAL_ERROR \"no apiary::annotations\")
endif()
# Every directory, not just the first: a consumer of a target that links
# apiary::annotations fails to generate on any that does not exist.
get_target_property(_incs apiary::annotations INTERFACE_INCLUDE_DIRECTORIES)
list(LENGTH _incs _n)
if(NOT _n EQUAL 1)
    message(FATAL_ERROR \"apiary::annotations should name one include directory, not: \${_incs}\")
endif()
if(NOT EXISTS \"\${_incs}/apiary/Annotations.hpp\")
    message(FATAL_ERROR \"apiary::annotations names \${_incs}, which has no apiary/Annotations.hpp\")
endif()
if(expect STREQUAL TOOL)
    if(NOT TARGET apiary::apiary OR NOT APIARY_TOOL_FOUND OR NOT APIARY_LLVM_VERSION_MAJOR)
        message(FATAL_ERROR \"the tool is missing: APIARY_TOOL_FOUND=\${APIARY_TOOL_FOUND}, \"
                            \"APIARY_LLVM_VERSION_MAJOR=\${APIARY_LLVM_VERSION_MAJOR}\")
    endif()
elseif(TARGET apiary::apiary OR APIARY_TOOL_FOUND OR DEFINED APIARY_LLVM_VERSION_MAJOR)
    message(FATAL_ERROR \"an install without the executable provides part of it\")
endif()
if(expect STREQUAL ANNOTATIONS)
    if(COMMAND apiary_add_bindings OR DEFINED APIARY_SCRIPTS_DIR)
        message(FATAL_ERROR \"an annotations-only install provides the helpers\")
    endif()
elseif(NOT COMMAND apiary_add_bindings OR NOT EXISTS \"\${APIARY_SCRIPTS_DIR}/apiary_aggregate_stubs.py\")
    message(FATAL_ERROR \"the helpers or their scripts are missing (APIARY_SCRIPTS_DIR=\${APIARY_SCRIPTS_DIR})\")
endif()
")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${_src}" -B "${_src}/build" -G "${GENERATOR}"
                "-DApiary_DIR=${_apiary_dir}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _out
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "consumer '${name}' against ${prefix}:\n${_out}")
    endif()
    message(STATUS "ok: ${name}")
endfunction()

_consume(annotations "${WORK_DIR}/annotations" annotations ANNOTATIONS)
_consume(annotations-without-tool "${WORK_DIR}/annotations" tool NOT_FOUND)
if(TOOL)
    _consume(both "${WORK_DIR}/both" tool TOOL)
else()
    _consume(both "${WORK_DIR}/both" annotations HELPERS)
    _consume(both-without-tool "${WORK_DIR}/both" tool NOT_FOUND)
endif()
