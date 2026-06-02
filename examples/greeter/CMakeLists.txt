#----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
#----------------------------------------------------------------------------------------------

# Apiary example: bind a small header-only C++ library into a Python extension.
#
# Build (with an installed Apiary on CMAKE_PREFIX_PATH):
#     cmake -S . -B build -DCMAKE_PREFIX_PATH=<apiary-install-prefix>
#     cmake --build build
#     PYTHONPATH=build python3 test_greeter.py
#
# Or vendor Apiary instead of installing it by replacing the find_package
# below with: add_subdirectory(<path-to-apiary> apiary)

cmake_minimum_required(VERSION 3.20)
project(apiary_greeter_example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Apiary REQUIRED)            # apiary::apiary, apiary::annotations, apiary_* helpers
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

# --- The C++ library being bound (header-only here) -----------------------
add_library(greeter INTERFACE)
target_include_directories(greeter INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Linking apiary::annotations puts the APIARY_* macro header on the include
# path (it expands to nothing in a normal compile) AND, because
# apiary_add_bindings collects this target's usage requirements, makes the
# header resolvable when the apiary tool parses the sources.
target_link_libraries(greeter INTERFACE apiary::annotations)

# --- Apiary codegen -------------------------------------------------------
# Probe the build compiler once for the include paths libtooling needs.
apiary_detect_toolchain(CXX_STANDARD 20)

set(_gen "${CMAKE_CURRENT_BINARY_DIR}/gen")

# Generate the binding TU (+ .pyi + docs JSON) for this module's headers.
apiary_add_bindings(
    BINDING DOCS_JSON
    HEADERS           "${CMAKE_CURRENT_SOURCE_DIR}/include/greeter/Greeter.hpp"
    SOURCE_INCLUDES   greeter/Greeter.hpp
    REGISTER_FUNCTION greeter_register_core
    DEPENDS_TARGETS   greeter
    OUTPUT_DIR        "${_gen}"
    OUTPUT_NAME       greeter_core
    CXX_STANDARD      20
    OUT_BINDING _tu  OUT_STUB _stub  OUT_DOCS_JSON _docs
)

# Assemble the module(s) into one extension. MAIN supplies the
# PYBIND11_MODULE body and includes the generated register header.
apiary_aggregate_extension(
    NAME greeter_ext
    MAIN     "${CMAKE_CURRENT_SOURCE_DIR}/src/module.cpp"
    BINDINGS ${_tu}
    MODULES  core
    REGISTER_PREFIX greeter_register_
    MODULES_HEADER      "${_gen}/include/greeter/Modules.hpp"
    MODULES_INCLUDE_DIR "${_gen}/include"
    STUBS ${_stub}  STUBS_TARGET greeter_stubs
    FRAG_DIR "${_gen}"  PKG_DIR "${CMAKE_CURRENT_BINARY_DIR}"
)

# Name the extension ``greeter`` and land it where ``import greeter`` finds it
# (PYTHONPATH=build), then link the library it binds.
set_target_properties(greeter_ext PROPERTIES
    OUTPUT_NAME greeter
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(greeter_ext PRIVATE greeter)
