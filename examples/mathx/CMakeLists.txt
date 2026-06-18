#----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
#----------------------------------------------------------------------------------------------

# Apiary example: document TWO source languages at once.
#
# A compiled C++ core (include/mathx/Vec.hpp) is bound into an extension, and a
# hand-written Python layer (python/mathx/) is added on top. Apiary documents
# both — the C++ via its Clang frontend, the Python via the static `ast`
# frontend — and merges them into one reference, with authored curation +
# articles under content/.
#
# Build (with an installed Apiary on CMAKE_PREFIX_PATH):
#     cmake -S . -B build -DCMAKE_PREFIX_PATH=<apiary-install-prefix>
#     cmake --build build                       # builds the extension + Python pkg
#     PYTHONPATH=build python3 test_mathx.py     # -> "mathx example: OK"
#     cmake --build build --target mathx_docs    # -> build/docs/*.rst (the merged reference)

cmake_minimum_required(VERSION 3.20)
project(apiary_mathx_example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Apiary REQUIRED)            # apiary::apiary, apiary::annotations, apiary_* helpers
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

# --- The C++ core being bound (header-only here) --------------------------
add_library(mathx_core INTERFACE)
target_include_directories(mathx_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(mathx_core INTERFACE apiary::annotations)

apiary_detect_toolchain(CXX_STANDARD 20)

set(_gen "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(_pkg "${CMAKE_CURRENT_BINARY_DIR}/mathx")   # the importable package dir

# --- 1. C++ frontend: bind the core + emit its docs-JSON fragment ----------
apiary_add_bindings(
    BINDING DOCS_JSON
    MODULE            mathx
    HEADERS           "${CMAKE_CURRENT_SOURCE_DIR}/include/mathx/Vec.hpp"
    SOURCE_INCLUDES   mathx/Vec.hpp
    REGISTER_FUNCTION mathx_register_core
    DEPENDS_TARGETS   mathx_core
    OUTPUT_DIR        "${_gen}"
    OUTPUT_NAME       mathx_core
    CXX_STANDARD      20
    OUT_BINDING _tu  OUT_STUB _stub  OUT_DOCS_JSON _cpp_docs
)

# --- 2. Python frontend: extract the hand-written layer's docs-JSON --------
apiary_add_python_docs(
    PACKAGE     mathx
    PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/python/mathx"
    SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT      "${_gen}/mathx.py.docs.json"
    OUT_PY_DOCS_JSON _py_docs
)

# --- 3. Assemble the extension + merge & render the unified docs -----------
# DOCS_JSON (C++) and PY_DOCS_JSON (Python) fold into one merge; DOCS_CONTENT_DIR
# adds the authored curation + articles.
apiary_aggregate_extension(
    NAME mathx_core_ext
    MAIN     "${CMAKE_CURRENT_SOURCE_DIR}/src/module.cpp"
    BINDINGS ${_tu}
    MODULES  core
    REGISTER_PREFIX mathx_register_
    MODULES_HEADER      "${_gen}/include/mathx/Modules.hpp"
    MODULES_INCLUDE_DIR "${_gen}/include"
    STUBS ${_stub}  STUBS_TARGET mathx_stubs  FRAG_DIR "${_gen}"  PKG_DIR "${_pkg}"
    DOCS_JSON        ${_cpp_docs}
    PY_DOCS_JSON     ${_py_docs}
    DOCS_TARGET      mathx_docs
    DOCS_OUTDIR      "${CMAKE_CURRENT_BINARY_DIR}/docs"
    DOCS_CONTENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/content"
)

# The compiled core is imported as ``mathx._core``: name it ``_core`` and land
# it inside the package dir.
set_target_properties(mathx_core_ext PROPERTIES
    OUTPUT_NAME _core
    LIBRARY_OUTPUT_DIRECTORY "${_pkg}"
)
target_link_libraries(mathx_core_ext PRIVATE mathx_core)

# Copy the hand-written Python sources next to the compiled core so
# ``PYTHONPATH=build python3 -c 'import mathx'`` works at runtime.
add_custom_target(mathx_python ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/python/mathx" "${_pkg}"
    COMMENT "mathx: staging Python package into ${_pkg}"
)
add_dependencies(mathx_python mathx_core_ext)
