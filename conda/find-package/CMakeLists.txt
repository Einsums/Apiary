# find_package(Apiary) as a consumer sees it. EXPECT_TOOL says whether the
# installed packages should include the codegen executable: ON for apiary,
# OFF for apiary-annotations installed alone. No compiler is needed.
cmake_minimum_required(VERSION 3.20)
project(find_apiary LANGUAGES NONE)

find_package(Apiary 1.3.1 REQUIRED CONFIG COMPONENTS annotations)
if(NOT TARGET apiary::annotations)
    message(FATAL_ERROR "find_package(Apiary) gave no apiary::annotations")
endif()
# Every include directory must exist, not just the first: a project consuming a
# target that links apiary::annotations fails to generate on any that does not.
get_target_property(_incs apiary::annotations INTERFACE_INCLUDE_DIRECTORIES)
list(LENGTH _incs _n)
if(NOT _n EQUAL 1 OR NOT EXISTS "${_incs}/apiary/Annotations.hpp")
    message(FATAL_ERROR "apiary::annotations should name the one directory holding apiary/Annotations.hpp, not: ${_incs}")
endif()

if(EXPECT_TOOL)
    find_package(Apiary 1.3.1 REQUIRED CONFIG COMPONENTS tool)
    if(NOT TARGET apiary::apiary OR NOT COMMAND apiary_add_bindings)
        message(FATAL_ERROR "the tool component lacks apiary::apiary or the helpers")
    endif()
else()
    if(APIARY_TOOL_FOUND OR TARGET apiary::apiary OR COMMAND apiary_add_bindings)
        message(FATAL_ERROR "apiary-annotations installed part of the tool")
    endif()
endif()
message(STATUS "Apiary ${Apiary_VERSION} at ${Apiary_DIR}, tool: ${APIARY_TOOL_FOUND}")
