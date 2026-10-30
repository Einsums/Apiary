# find_package(Apiary) as a consumer sees it. EXPECT_TOOL says whether the
# installed packages should include the codegen executable: ON for apiary,
# OFF for apiary-annotations installed alone. No compiler is needed.
cmake_minimum_required(VERSION 3.20)
project(find_apiary LANGUAGES NONE)

find_package(Apiary 1.3 REQUIRED CONFIG COMPONENTS annotations)
if(NOT TARGET apiary::annotations)
    message(FATAL_ERROR "find_package(Apiary) gave no apiary::annotations")
endif()
get_target_property(_inc apiary::annotations INTERFACE_INCLUDE_DIRECTORIES)
list(GET _inc 0 _inc)
if(NOT EXISTS "${_inc}/apiary/Annotations.hpp")
    message(FATAL_ERROR "apiary::annotations names ${_inc}, which has no apiary/Annotations.hpp")
endif()

if(EXPECT_TOOL)
    find_package(Apiary 1.3 REQUIRED CONFIG COMPONENTS tool)
    if(NOT TARGET apiary::apiary OR NOT COMMAND apiary_add_bindings)
        message(FATAL_ERROR "the tool component lacks apiary::apiary or the helpers")
    endif()
else()
    if(APIARY_TOOL_FOUND OR TARGET apiary::apiary OR COMMAND apiary_add_bindings)
        message(FATAL_ERROR "apiary-annotations installed part of the tool")
    endif()
endif()
message(STATUS "Apiary ${Apiary_VERSION} at ${Apiary_DIR}, tool: ${APIARY_TOOL_FOUND}")
