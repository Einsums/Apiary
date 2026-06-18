#----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
#----------------------------------------------------------------------------------------------

# apiary — libtooling-based codegen for Python bindings.
#
# Walks Einsums headers, finds declarations marked with APIARY_*
# macros (see libs/Einsums/Python/include/Einsums/Python/Annotations.hpp),
# and emits a pybind11 binding TU per module. Designed to be invoked by
# einsums_add_module() so that adding a new C++ feature requires no manual
# binding work — just annotate the declaration and rebuild.
#
# This directory provides two independent pieces:
#   * apiary::annotations - a header-only INTERFACE target carrying the
#     APIARY_* macros. Always defined; needs no Clang/LLVM. Every consumer
#     that merely annotates declarations (i.e. every Einsums TU) depends on
#     just this.
#   * apiary            - the libtooling codegen executable. Built only when
#     APIARY_BUILD_TOOL is ON (the default). It links libtooling
#     (clangTooling, clangFrontend, clangAST, clangBasic, ...) and
#     llvmSupport, so it needs the Clang/LLVM development packages; it
#     mirrors the discovery pattern used by EinsumsClangd's clang-tidy
#     module.
#
# Turn APIARY_BUILD_TOOL OFF to vendor the annotation header without pulling
# in Clang/LLVM — e.g. a pure-C++ build that neither generates bindings nor
# builds docs. Bindings generation and the docs C++ reference both need the
# executable, so the consumer enables APIARY_BUILD_TOOL for those.

# Allow this directory to act as a standalone CMake project for local
# iteration without rebuilding the parent — useful while hacking on the
# tool itself.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(apiary LANGUAGES CXX)
endif()

set(APIARY_VERSION 0.1.0)

# Building the libtooling executable needs the Clang/LLVM development
# packages. Consumers that only want the annotation header (apiary::annotations)
# can switch this OFF to avoid that dependency entirely.
option(APIARY_BUILD_TOOL "Build the libtooling codegen executable (requires Clang/LLVM dev)" ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
# Consumer-facing helper functions (apiary_detect_toolchain, ...). Available
# in both add_subdirectory and find_package(Apiary) modes.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApiaryHelpers.cmake)
# Bundled Python scripts (stub aggregation, doc rendering) the helpers invoke.
# For add_subdirectory consumers this points in-tree; find_package consumers
# get the installed location from ApiaryConfig.cmake instead.
set(APIARY_SCRIPTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scripts" CACHE INTERNAL "apiary bundled scripts dir")

if(APIARY_BUILD_TOOL)
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
    clangIndex
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

# Namespaced alias so add_subdirectory consumers use the same apiary::apiary
# they would get from find_package(Apiary).
add_executable(apiary::apiary ALIAS apiary)
endif() # APIARY_BUILD_TOOL

# --- Annotation contract (consumer-facing header) -------------------------
# The APIARY_* binding-annotation macros. Consumers link this INTERFACE
# target instead of hardcoding the include path.
add_library(apiary_annotations INTERFACE)
target_include_directories(apiary_annotations INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(apiary_annotations PROPERTIES EXPORT_NAME annotations)
add_library(apiary::annotations ALIAS apiary_annotations)

# --- Install / export (find_package(Apiary)) ------------------------------
set(_apiary_cmakedir "${CMAKE_INSTALL_LIBDIR}/cmake/Apiary")

# apiary_annotations (and the header it carries) install unconditionally so a
# find_package(Apiary) / install tree always provides the macros, even in a
# header-only (APIARY_BUILD_TOOL=OFF) configuration. The executable is added
# to the same export set only when it was built; ApiaryTargets.cmake then
# defines apiary::apiary just for tool-enabled installs.
set(_apiary_install_targets apiary_annotations)
if(APIARY_BUILD_TOOL)
    list(PREPEND _apiary_install_targets apiary)
endif()
install(TARGETS ${_apiary_install_targets}
    EXPORT ApiaryTargets
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/apiary DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT ApiaryTargets
    NAMESPACE apiary::
    DESTINATION ${_apiary_cmakedir}
    FILE ApiaryTargets.cmake
)

# Bundled Python scripts (stub aggregation + doc rendering) the helper
# functions invoke; consumers locate them via APIARY_SCRIPTS_DIR.
install(DIRECTORY scripts/ DESTINATION ${CMAKE_INSTALL_DATADIR}/apiary/scripts)
install(FILES cmake/ApiaryHelpers.cmake DESTINATION ${_apiary_cmakedir})

set(APIARY_SCRIPTS_DIR "${CMAKE_INSTALL_DATADIR}/apiary/scripts")
configure_package_config_file(
    cmake/ApiaryConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/ApiaryConfig.cmake"
    INSTALL_DESTINATION ${_apiary_cmakedir}
    PATH_VARS APIARY_SCRIPTS_DIR
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ApiaryConfigVersion.cmake"
    VERSION ${APIARY_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ApiaryConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ApiaryConfigVersion.cmake"
    DESTINATION ${_apiary_cmakedir}
)

# --- Smoke tests ----------------------------------------------------------
# Phase-2 fixture suite. Each fixture is a small annotated header and the
# script asserts substrings in the IR dump. Phase 3 will replace this with
# golden-output diffs once the emitter output stabilizes.
if(EINSUMS_WITH_TESTS OR CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    # The smoke/golden/pyi suites drive the executable, so they only register
    # when it was built. doc_lint below is pure Python and runs either way.
    if(APIARY_BUILD_TOOL)
        add_test(NAME apiary_phase2_smoke
            COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_smoke.sh"
                    "$<TARGET_FILE:apiary>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )
        add_test(NAME apiary_phase3_golden
            COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_golden.sh"
                    "$<TARGET_FILE:apiary>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )
        # .pyi golden suite — diffs the generated stub for each fixture
        # against a committed golden. Catches drift in per-instantiation
        # type resolution, docstring extraction, property merge, etc.
        add_test(NAME apiary_pyi_golden
            COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_pyi_golden.sh"
                    "$<TARGET_FILE:apiary>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )
        # Sharded emission (--max-defs-per-tu / --plan): asserts plan==emit,
        # multiple balanced shards, the dispatcher wiring, and that no binding
        # is dropped or duplicated relative to the single-TU output.
        add_test(NAME apiary_shard
            COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_shard.sh"
                    "$<TARGET_FILE:apiary>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )
    endif()
    # apiary_doc_lint.py: doc-quality validator over the docs-JSON IR. Pure Python,
    # so it needs no apiary binary — it diffs diagnostics for a seeded-drift
    # fixture against a golden. Prefer a CMake-found interpreter, else python3.
    find_package(Python COMPONENTS Interpreter QUIET)
    add_test(NAME apiary_doc_lint
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_doc_lint.sh"
                "$<IF:$<BOOL:${Python_EXECUTABLE}>,${Python_EXECUTABLE},python3>"
    )
    # Static Python frontend: apiary_py_extract.py -> apiary_merge_docs_json.py ->
    # apiary_render_docs_rst.py. Pure Python (a committed C++-origin fragment fixture
    # stands in for the C++ tool), so it runs without the apiary binary. Asserts
    # on the structured docs.json plus one loose end-to-end .rst presence check.
    add_test(NAME apiary_py_extract
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_py_extract.sh"
                "$<IF:$<BOOL:${Python_EXECUTABLE}>,${Python_EXECUTABLE},python3>"
    )
endif()
