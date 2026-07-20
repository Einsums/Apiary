#----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
#----------------------------------------------------------------------------------------------

# Script-mode wrapper (``cmake -P``) that runs apiary and filters libtooling's
# per-file progress chatter out of stderr.
#
# ``clang::tooling::ClangTool::run`` unconditionally writes
#
#     [1/12] Processing file /abs/path/Foo.hpp.
#
# to stderr for every source path, and that print is not gated on anything the
# public ClangTool API exposes - there is no setPrintProgress in this LLVM. For
# a codegen step invoked over a module's whole header list it is pure noise, and
# it interleaves confusingly with ninja's own ``[287/639]`` counters.
#
# A blanket ``2>/dev/null`` is not acceptable: the same stream carries real
# parse errors. So capture stderr, drop ONLY lines matching the progress shape,
# re-emit everything else, and propagate the exit status faithfully.
#
# Usage (from add_custom_command, under VERBATIM):
#
#   COMMAND ${CMAKE_COMMAND}
#           "-DAPIARY_COMMAND=<exe>;<arg>;<arg>..."
#           -P ${APIARY_HELPERS_DIR}/ApiaryRun.cmake
#
# The value is a normal CMake list (semicolon-separated). Arguments may contain
# spaces; they must not contain semicolons.

if(NOT DEFINED APIARY_COMMAND)
    message(FATAL_ERROR "ApiaryRun: APIARY_COMMAND is required")
endif()

# stdout is deliberately NOT captured so it passes through to the build log
# untouched (``--plan`` mode writes its shard list there).
execute_process(
    COMMAND ${APIARY_COMMAND}
    RESULT_VARIABLE _apiary_rc
    ERROR_VARIABLE _apiary_err
)

# Drop the progress lines. Anchored on the counter so a diagnostic that merely
# quotes the phrase survives.
string(REGEX REPLACE "\\[[0-9]+/[0-9]+\\] Processing file [^\n]*\n?" ""
       _apiary_err "${_apiary_err}")

string(STRIP "${_apiary_err}" _apiary_err_stripped)
if(_apiary_err_stripped)
    # NOTICE goes to stderr, keeping diagnostics on the stream tools expect.
    message(NOTICE "${_apiary_err}")
endif()

if(NOT _apiary_rc EQUAL 0)
    list(GET APIARY_COMMAND 0 _apiary_exe)
    message(FATAL_ERROR "apiary failed (exit ${_apiary_rc}): ${_apiary_exe}")
endif()
