#----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
#----------------------------------------------------------------------------------------------

# A consumer whose module set changes between builds of one tree, for
# tests/run_module_removal.sh. Laid out like examples/mathx: the extension is
# the ``_core`` of a ``demo`` package staged into the build tree.

cmake_minimum_required(VERSION 3.20)
project(apiary_module_removal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WITH_GONE "build the module that is removed later" ON)
option(WITH_SOLO "build the solo submodule's only module" ON)

find_package(Apiary REQUIRED)
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

add_library(demo INTERFACE)
target_include_directories(demo INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(demo INTERFACE apiary::annotations)

apiary_detect_toolchain(CXX_STANDARD 20)

set(_gen "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(_pkg "${CMAKE_CURRENT_BINARY_DIR}/demo")

set(_modules core)
if(WITH_GONE)
    list(APPEND _modules gone)
endif()
if(WITH_SOLO)
    list(APPEND _modules solo)
endif()

set(_tus "")
set(_stubs "")
set(_docs "")
foreach(_m IN LISTS _modules)
    apiary_add_bindings(
        BINDING DOCS_JSON
        MODULE            demo
        HEADERS           "${CMAKE_CURRENT_SOURCE_DIR}/include/demo/${_m}.hpp"
        SOURCE_INCLUDES   demo/${_m}.hpp
        REGISTER_FUNCTION demo_register_${_m}
        DEPENDS_TARGETS   demo
        OUTPUT_DIR        "${_gen}"
        OUTPUT_NAME       demo_${_m}
        CXX_STANDARD      20
        OUT_BINDING _tu  OUT_STUB _stub  OUT_DOCS_JSON _doc
    )
    list(APPEND _tus ${_tu})
    list(APPEND _stubs ${_stub})
    list(APPEND _docs ${_doc})
endforeach()

apiary_aggregate_extension(
    NAME demo_ext
    MAIN     "${CMAKE_CURRENT_SOURCE_DIR}/src/module.cpp"
    BINDINGS ${_tus}
    MODULES  ${_modules}
    REGISTER_PREFIX demo_register_
    MODULES_HEADER      "${_gen}/include/demo/Modules.hpp"
    MODULES_INCLUDE_DIR "${_gen}/include"
    STUBS ${_stubs}  STUBS_TARGET demo_stubs  FRAG_DIR "${_gen}"  PKG_DIR "${_pkg}"
    DOCS_JSON   ${_docs}
    DOCS_TARGET demo_docs
    DOCS_OUTDIR "${CMAKE_CURRENT_BINARY_DIR}/docs"
)

set_target_properties(demo_ext PROPERTIES
    OUTPUT_NAME _core
    LIBRARY_OUTPUT_DIRECTORY "${_pkg}"
)
target_link_libraries(demo_ext PRIVATE demo)

# Stage the Python package beside the extension, as examples/mathx does.
add_custom_target(demo_python ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/python/demo" "${_pkg}"
)
add_dependencies(demo_python demo_ext)
