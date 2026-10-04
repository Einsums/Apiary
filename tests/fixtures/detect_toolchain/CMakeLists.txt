#----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
#----------------------------------------------------------------------------------------------

# Configure-only project for tests/run_detect_toolchain.sh: runs
# apiary_detect_toolchain() against stand-in apiary and compiler executables,
# so which builtin headers it picks can be checked without a real toolchain.

cmake_minimum_required(VERSION 3.20)
project(apiary_detect_toolchain_test LANGUAGES NONE)

include("${APIARY_HELPERS}")

# What ApiaryConfig.cmake records for an installed apiary.
set(APIARY_LLVM_VERSION_MAJOR 23)
if(FAKE_APIARY)
    add_executable(apiary::apiary IMPORTED)
    set_target_properties(apiary::apiary PROPERTIES IMPORTED_LOCATION "${FAKE_APIARY}")
endif()
# Only probed, never used to build: the project enables no language.
set(CMAKE_CXX_COMPILER "${FAKE_CXX}")

apiary_detect_toolchain(CXX_STANDARD 20)
message(STATUS "RESULT resource-dir=${APIARY_RESOURCE_DIR}")
message(STATUS "RESULT flags=${APIARY_SYSTEM_FLAGS}")
