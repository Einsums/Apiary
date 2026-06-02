#----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
#----------------------------------------------------------------------------------------------

# apiary — libtooling-based codegen for Python bindings.
#
# Walks Einsums headers, finds declarations marked with EINSUMS_PYBIND_*
# macros (see libs/Einsums/Python/include/Einsums/Python/Annotations.hpp),
# and emits a pybind11 binding TU per module. Designed to be invoked by
# einsums_add_module() so that adding a new C++ feature requires no manual
# binding work — just annotate the declaration and rebuild.
#
# This directory is gated by EINSUMS_BUILD_PYTHON at the project root;
# the parent CMakeLists.txt only descends here when that option is ON.
#
# The tool links against libtooling (clangTooling, clangFrontend, clangAST,
# clangBasic, ...) and llvmSupport. It mirrors the discovery pattern used
# by EinsumsClangd's clang-tidy module.

# Allow this directory to act as a standalone CMake project for local
# iteration without rebuilding the parent — useful while hacking on the
# tool itself.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(apiary LANGUAGES CXX)
endif()

# --- Locate Clang/LLVM development packages -------------------------------
# find_package(Clang) brings in the AST, ASTMatchers, Frontend, Tooling,
# and the imported targets we link against. The conda einsums-dev env ships
# these.
find_package(Clang REQUIRED CONFIG)
find_package(LLVM REQUIRED CONFIG)

message(STATUS "apiary: using LLVM ${LLVM_VERSION} from ${LLVM_DIR}")
message(STATUS "apiary: using Clang from ${Clang_DIR}")

# --- Tool executable ------------------------------------------------------
add_executable(apiary
    src/main.cpp
    src/IR.cpp
    src/AnnotationParser.cpp
    src/DocComment.cpp
    src/DocExtractor.cpp
    src/DocsJson.cpp
    src/Emitter.cpp
    src/InstantiateParser.cpp
    src/MacroScanner.cpp
    src/Properties.cpp
    src/PyiEmitter.cpp
    src/PythonOverloads.cpp
    src/TypeTranslator.cpp
    src/Visitor.cpp
)

target_include_directories(apiary PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Libtooling pulls in everything we need: ASTMatchers + Frontend +
# Tooling + Driver + Sema. We don't link the full LLVM since libtooling's
# IMPORTED targets resolve transitive dependencies.
target_link_libraries(apiary PRIVATE
    clangTooling
    clangToolingCore
    clangFormat
    clangFrontend
    clangSerialization
    clangSema
    clangAST
    clangASTMatchers
    clangBasic
    clangLex
    clangDriver
    LLVMSupport
)

# Match LLVM's own dialect floor. LLVM headers commit to C++17, and
# libtooling has historically been picky about RTTI. Disable RTTI to match
# the LLVM build, otherwise downcasts via dyn_cast<> can hit ABI mismatches
# on some platforms.
set_target_properties(apiary PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_compile_options(apiary PRIVATE -fno-rtti)

# Conventional install location next to the rest of Einsums' CLI tools.
install(TARGETS apiary
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT einsums_runtime
)

# --- Smoke tests ----------------------------------------------------------
# Phase-2 fixture suite. Each fixture is a small annotated header and the
# script asserts substrings in the IR dump. Phase 3 will replace this with
# golden-output diffs once the emitter output stabilizes.
if(EINSUMS_WITH_TESTS OR CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_test(NAME einsums_pybind_phase2_smoke
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_smoke.sh"
                "$<TARGET_FILE:apiary>"
                "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    add_test(NAME einsums_pybind_phase3_golden
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_golden.sh"
                "$<TARGET_FILE:apiary>"
                "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    # .pyi golden suite — diffs the generated stub for each fixture
    # against a committed golden. Catches drift in per-instantiation
    # type resolution, docstring extraction, property merge, etc.
    add_test(NAME einsums_pybind_pyi_golden
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_pyi_golden.sh"
                "$<TARGET_FILE:apiary>"
                "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
endif()
