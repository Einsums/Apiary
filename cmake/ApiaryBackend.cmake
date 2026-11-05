#----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
#----------------------------------------------------------------------------------------------

# ApiaryBackend - the Python-binding library a build targets: choosing it, finding it, and creating
# extension modules with it, so a consumer writes no pybind11- or nanobind-specific CMake.
#
# Installed with the annotations, not the tool: a package that builds hand-written extension
# modules against an installed library (an Einsums stage module, say) needs this and
# <apiary/Python.hpp>, and neither needs Clang/LLVM. ApiaryConfig.cmake includes it whatever
# components were asked for, and ApiaryHelpers.cmake includes it for the codegen helpers.
#
# The build-wide choice, read by everything below when no BACKEND is passed:
#
#   APIARY_BACKEND       pybind11 (the default) or nanobind. A project that builds another apiary
#                        consumer inside its own (Einsums builds Waggle) hands it the same choice.
#   APIARY_STABLE_ABI    Build nanobind extension modules against Python's stable ABI (abi3): one
#                        module per platform that loads on every Python from 3.12 on.
#
# The functions:
#
#   apiary_find_backend([BACKEND <pybind11|nanobind>] [VERSION <v> [EXACT]])
#       Find Python and the binding library, enforce its floor (nanobind 3.0) and any VERSION,
#       and make it the default for the rest of the calling directory: sets APIARY_BACKEND and
#       APIARY_BACKEND_VERSION there. VERSION is a minimum, or with EXACT the one version to
#       accept, which is how an installed package pins its consumers to the release it was built
#       with.
#
#   apiary_add_extension_module(<name> [BACKEND <lib>] [NOMINSIZE] [OPTIONS <opt>...] <source>...)
#       A Python extension module from hand-written sources, built with the binding library, and
#       with APIARY_BACKEND_PYBIND11 or APIARY_BACKEND_NANOBIND defined so <apiary/Python.hpp>
#       knows which. NOMINSIZE keeps the build type's optimization level (nanobind otherwise
#       builds for size; pybind11 never does). OPTIONS go to the library's own add-module command
#       unchanged.
#
# apiary_aggregate_extension creates its module the same way, so a generated extension and a
# hand-written one are built alike.

include_guard(GLOBAL)

set(APIARY_BACKEND
    "pybind11"
    CACHE STRING "Python-binding library apiary targets: pybind11 or nanobind"
)
set_property(CACHE APIARY_BACKEND PROPERTY STRINGS pybind11 nanobind)
option(APIARY_STABLE_ABI "Build nanobind extension modules against Python's stable ABI (abi3, Python 3.12+)" OFF)

# The BACKEND values the helpers accept, so a consumer can ask whether the Apiary it found can
# target a library before passing it:
#
#     get_property(_backends GLOBAL PROPERTY APIARY_BINDING_BACKENDS)
#     if(NOT "nanobind" IN_LIST _backends) ...
#
# A global property rather than a variable so that it is visible from every directory scope.
set_property(GLOBAL PROPERTY APIARY_BINDING_BACKENDS pybind11 nanobind)

# Internal: a BACKEND argument, or the build-wide choice when there is none, validated.
function(_apiary_resolve_backend caller value out_var)
    if(NOT value)
        set(value "${APIARY_BACKEND}")
    endif()
    if(NOT value)
        set(value "pybind11")
    endif()
    get_property(_backends GLOBAL PROPERTY APIARY_BINDING_BACKENDS)
    if(NOT value IN_LIST _backends)
        list(JOIN _backends " or " _spelled)
        message(FATAL_ERROR "${caller}: the binding library must be ${_spelled}, not '${value}'")
    endif()
    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

# A macro, not a function: find_package has to run in the caller's scope. Both libraries' config
# files set variables their add-module commands read later, and inside a function those would be
# gone by the time the consumer creates a module.
macro(apiary_find_backend)
    cmake_parse_arguments(_apiary_fb "EXACT" "BACKEND;VERSION" "" ${ARGN})
    if(_apiary_fb_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "apiary_find_backend: unexpected arguments: ${_apiary_fb_UNPARSED_ARGUMENTS}")
    endif()
    if(_apiary_fb_EXACT AND NOT _apiary_fb_VERSION)
        message(FATAL_ERROR "apiary_find_backend: EXACT needs a VERSION")
    endif()
    _apiary_resolve_backend(apiary_find_backend "${_apiary_fb_BACKEND}" _apiary_fb_backend)
    if(APIARY_STABLE_ABI AND _apiary_fb_backend STREQUAL "pybind11")
        message(FATAL_ERROR "APIARY_STABLE_ABI needs APIARY_BACKEND=nanobind: pybind11 has no stable-ABI build.")
    endif()

    # Python first: both libraries' packages expect its targets. Development.SABIModule is what a
    # stable-ABI module links against; it exists only from 3.12, where nanobind uses it.
    set(_apiary_fb_optional "")
    if(APIARY_STABLE_ABI)
        list(APPEND _apiary_fb_optional Development.SABIModule)
    endif()
    if(NOT TARGET Python::Module OR (APIARY_STABLE_ABI AND NOT TARGET Python::SABIModule))
        find_package(Python COMPONENTS Interpreter Development.Module OPTIONAL_COMPONENTS ${_apiary_fb_optional} REQUIRED)
    endif()

    if(_apiary_fb_backend STREQUAL "nanobind")
        if(NOT COMMAND nanobind_add_module)
            # nanobind installs its CMake package inside the Python package, not under a prefix
            # find_package searches, so ask the interpreter where.
            execute_process(
                COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
                OUTPUT_VARIABLE _apiary_fb_nanobind_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            # No version here: nanobind's version file accepts only its own major version, so
            # asking for 3.0 would refuse every later major. The floor is checked below.
            find_package(nanobind CONFIG REQUIRED HINTS "${_apiary_fb_nanobind_dir}")
        endif()
        set(_apiary_fb_found "${nanobind_VERSION}")
        # 3.0 is the floor: 2.x headers do not compile under Py_LIMITED_API, so no 2.x build can be a
        # stable-ABI one, and a 2.x extension cannot share types with a 3.x one in any case.
        if(_apiary_fb_found VERSION_LESS 3.0)
            message(FATAL_ERROR "apiary_find_backend: nanobind 3.0 or newer is needed; found "
                                "${_apiary_fb_found} at ${nanobind_DIR}.")
        endif()
    else()
        if(NOT COMMAND pybind11_add_module)
            find_package(pybind11 CONFIG REQUIRED)
        endif()
        set(_apiary_fb_found "${pybind11_VERSION}")
    endif()

    # Checked by hand, for the same reason as the floor.
    if(_apiary_fb_VERSION)
        if(_apiary_fb_EXACT AND NOT _apiary_fb_found VERSION_EQUAL _apiary_fb_VERSION)
            message(FATAL_ERROR "apiary_find_backend: ${_apiary_fb_backend} ${_apiary_fb_VERSION} exactly is "
                                "needed; found ${_apiary_fb_found}.")
        elseif(_apiary_fb_found VERSION_LESS _apiary_fb_VERSION)
            message(FATAL_ERROR "apiary_find_backend: ${_apiary_fb_backend} ${_apiary_fb_VERSION} or newer is "
                                "needed; found ${_apiary_fb_found}.")
        endif()
    endif()

    # The default for the rest of this directory and the ones below it.
    set(APIARY_BACKEND "${_apiary_fb_backend}")
    set(APIARY_BACKEND_VERSION "${_apiary_fb_found}")

    foreach(_apiary_fb_var backend optional nanobind_dir found EXACT BACKEND VERSION UNPARSED_ARGUMENTS
                           KEYWORDS_MISSING_VALUES)
        unset(_apiary_fb_${_apiary_fb_var})
    endforeach()
    unset(_apiary_fb_var)
endmacro()

# Internal: create extension module <name> from SOURCES with <backend>, as every helper that makes
# one does. NOMINSIZE and APIARY_STABLE_ABI are translated for the library; OPTIONS pass through.
function(_apiary_add_module caller name backend)
    cmake_parse_arguments(PARSE_ARGV 3 _M "NOMINSIZE" "" "OPTIONS;SOURCES")
    if(APIARY_STABLE_ABI AND backend STREQUAL "pybind11")
        message(FATAL_ERROR "${caller}(${name}): APIARY_STABLE_ABI needs nanobind; pybind11 has no stable-ABI build.")
    endif()

    set(_options ${_M_OPTIONS})
    if(backend STREQUAL "nanobind")
        if(NOT COMMAND nanobind_add_module)
            message(FATAL_ERROR "${caller}(${name}): nanobind is not found; call apiary_find_backend() first.")
        endif()
        if(DEFINED nanobind_VERSION AND nanobind_VERSION VERSION_LESS 3.0)
            message(FATAL_ERROR "${caller}(${name}): nanobind 3.0 or newer is needed; found "
                                "${nanobind_VERSION} at ${nanobind_DIR}.")
        endif()
        if(_M_NOMINSIZE AND NOT "NOMINSIZE" IN_LIST _options)
            list(APPEND _options NOMINSIZE)
        endif()
        if(APIARY_STABLE_ABI AND NOT "STABLE_ABI" IN_LIST _options)
            list(APPEND _options STABLE_ABI)
        endif()
        # nanobind drops STABLE_ABI without a word when Python::SABIModule is missing, and builds an
        # ordinary per-version extension instead. Below 3.12 dropping it is nanobind's documented
        # behavior, so only a Python that could have had the stable ABI is refused.
        if("STABLE_ABI" IN_LIST _options AND Python_VERSION VERSION_GREATER_EQUAL 3.12
           AND NOT TARGET Python::SABIModule)
            message(FATAL_ERROR "${caller}(${name}): STABLE_ABI needs the Python::SABIModule target, which "
                                "nanobind otherwise silently goes without. apiary_find_backend() asks for it under "
                                "APIARY_STABLE_ABI; otherwise add Development.SABIModule to find_package(Python ...).")
        endif()
        nanobind_add_module(${name} ${_options} ${_M_SOURCES})
        target_compile_definitions(${name} PRIVATE APIARY_BACKEND_NANOBIND)
    else()
        if(NOT COMMAND pybind11_add_module)
            message(FATAL_ERROR "${caller}(${name}): pybind11 is not found; call apiary_find_backend() first.")
        endif()
        # pybind11 keeps the build type's optimization level unless asked otherwise, so NOMINSIZE
        # needs no translation.
        pybind11_add_module(${name} ${_options} ${_M_SOURCES})
        target_compile_definitions(${name} PRIVATE APIARY_BACKEND_PYBIND11)
    endif()

    # <apiary/Python.hpp> sits beside the annotation header.
    if(TARGET apiary::annotations)
        target_link_libraries(${name} PRIVATE apiary::annotations)
    endif()
endfunction()

function(apiary_add_extension_module name)
    cmake_parse_arguments(PARSE_ARGV 1 _E "NOMINSIZE" "BACKEND" "OPTIONS")
    if(NOT _E_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "apiary_add_extension_module(${name}): no sources")
    endif()
    _apiary_resolve_backend(apiary_add_extension_module "${_E_BACKEND}" _backend)
    set(_nominsize "")
    if(_E_NOMINSIZE)
        set(_nominsize NOMINSIZE)
    endif()
    _apiary_add_module(
        apiary_add_extension_module ${name} ${_backend} ${_nominsize}
        OPTIONS ${_E_OPTIONS}
        SOURCES ${_E_UNPARSED_ARGUMENTS}
    )
endfunction()
