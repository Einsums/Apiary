#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
#
# Spelling for the flags the compile-based suites need, per compiler driver.
#
# Sourced, not executed. Two suites hand a generated binding TU to the project's
# own C++ compiler and ask only "does this parse". Which is a different question
# on Windows, where the supported compiler is clang-cl: a clang frontend behind
# an MSVC-style driver, which does NOT understand the GNU spellings.
#
# Both differences fail quietly, and both read as an apiary defect rather than a
# harness one, which is why they are worth naming here:
#
#   * -std=c++20 is not an MSVC-style flag. clang-cl ignores it (with a warning
#     nobody sees in a passing run) and falls back to its own default, which is
#     older than C++20. The suite then reports "unknown type name 'concept'" on
#     a fixture whose whole purpose is to pin a C++20 shape.
#   * MSVC-style drivers disable exceptions unless asked. pybind11 throws in its
#     headers, so every generated TU fails with "cannot use 'throw' with
#     exceptions disabled" - inside pybind11, far from anything apiary wrote.
#
# Sets APIARY_CXX_SYNTAX_FLAGS rather than echoing, because the flags have to
# survive as separate argv entries and macOS still ships bash 3.2 (no mapfile,
# no readarray).
apiary_set_cxx_syntax_flags() {
    case "$(basename "$1")" in
        clang-cl | clang-cl.exe | clang-cl-* | cl | cl.exe)
            APIARY_CXX_SYNTAX_FLAGS=(-fsyntax-only /std:c++20 /EHsc)
            ;;
        *)
            APIARY_CXX_SYNTAX_FLAGS=(-fsyntax-only -std=c++20)
            ;;
    esac
}
