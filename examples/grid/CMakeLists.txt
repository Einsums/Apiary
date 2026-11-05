#----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
#----------------------------------------------------------------------------------------------

# Apiary example: Python's container protocols from plain C++.
#
# include/grid/Grid.hpp declares a dense array whose buffer, index and iterator
# protocols are described by APIARY_*_STD directives naming ordinary member
# functions. Apiary writes the protocol code for the selected binding library,
# so the same header builds a working extension under pybind11 and nanobind.
#
# Build (with an installed Apiary on CMAKE_PREFIX_PATH):
#     cmake -S . -B build -DCMAKE_PREFIX_PATH=<apiary-install-prefix> [-DAPIARY_BACKEND=nanobind]
#     cmake --build build
#     PYTHONPATH=build python3 test_grid.py

cmake_minimum_required(VERSION 3.20)
project(apiary_grid_example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Apiary REQUIRED COMPONENTS tool)

# The binding library: APIARY_BACKEND, pybind11 by default or nanobind, found
# here with the Python it builds for. Everything below follows from it, and
# src/module.cpp is written for either. -DAPIARY_STABLE_ABI=ON makes a nanobind
# build one stable-ABI module for every Python from 3.12 on.
apiary_find_backend()

add_library(grid INTERFACE)
target_include_directories(grid INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(grid INTERFACE apiary::annotations)

apiary_detect_toolchain(CXX_STANDARD 20)

set(_gen "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(_pkg "${CMAKE_CURRENT_BINARY_DIR}/grid")

apiary_add_bindings(
    BINDING
    MODULE            grid
    HEADERS           "${CMAKE_CURRENT_SOURCE_DIR}/include/grid/Grid.hpp"
    SOURCE_INCLUDES   grid/Grid.hpp
    REGISTER_FUNCTION grid_register_core
    DEPENDS_TARGETS   grid
    OUTPUT_DIR        "${_gen}"
    OUTPUT_NAME       grid_core
    CXX_STANDARD      20
    OUT_BINDING _tu  OUT_STUB _stub
)

apiary_aggregate_extension(
    NAME grid_ext
    MAIN     "${CMAKE_CURRENT_SOURCE_DIR}/src/module.cpp"
    BINDINGS ${_tu}
    MODULES  core
    REGISTER_PREFIX grid_register_
    MODULES_HEADER      "${_gen}/include/grid/Modules.hpp"
    MODULES_INCLUDE_DIR "${_gen}/include"
    STUBS ${_stub}  STUBS_TARGET grid_stubs
    FRAG_DIR "${_gen}"  PKG_DIR "${_pkg}"
    PY_HELPERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/python/grid"
)

set_target_properties(grid_ext PROPERTIES
    OUTPUT_NAME _core
    LIBRARY_OUTPUT_DIRECTORY "${_pkg}"
)
target_link_libraries(grid_ext PRIVATE grid)

add_custom_target(grid_python ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/python/grid" "${_pkg}"
    COMMENT "grid: staging Python package into ${_pkg}"
)
add_dependencies(grid_python grid_ext)
