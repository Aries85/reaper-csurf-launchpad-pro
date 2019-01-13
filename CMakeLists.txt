# https://stackoverflow.com/questions/17511496/how-to-create-a-shared-library-with-cmake

cmake_minimum_required(VERSION 3.12)
project(reaper_csurf_launchpadpro VERSION 1.0.0 DESCRIPTION "REAPER Control Surface Library for Launchpad Pro")

set(CMAKE_CXX_STANDARD 14)

configure_file (
        "${PROJECT_SOURCE_DIR}/launchpad_version.h.in"
        "${PROJECT_BINARY_DIR}/launchpad_version.h"
)

set (WDL_DIR ../WDL)
set (RESOURCE_FILE "res.rc")
set (LINKED_LIBRARIES "")

include_directories(
        ${WDL_DIR}
)

add_library(reaper_csurf_launchpadpro SHARED
        csurf_launchpadpro.cpp
        ${WDL_DIR}/swell/swell-modstub.mm
        launchpad_version.h.in)

# remove "lib" prefix in library, REAPER library must start with name "reaper"
SET_TARGET_PROPERTIES(reaper_csurf_launchpadpro PROPERTIES PREFIX "")

if (APPLE)
    find_library(AppKit AppKit)
    if (NOT AppKit)
        message(FATAL_ERROR "AppKit not found")
    endif()
    set(LINKED_LIBRARIES ${LINKED_LIBRARIES} ${AppKit})

    SET (GENERATED_RESOURCE_HEADERS
            res.rc_mac_dlg
            res.rc_mac_menu
            )

    add_custom_command(OUTPUT ${GENERATED_RESOURCE_HEADERS}
            COMMAND php "${CMAKE_CURRENT_SOURCE_DIR}/${WDL_DIR}/swell/mac_resgen.php" "${RESOURCE_FILE}"
            COMMAND touch ${GENERATED_RESOURCE_HEADERS}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res.rc"
            COMMENT "Compiling resource file to header file for Swell"
            VERBATIM
            )

    add_custom_target(compile_resources DEPENDS ${GENERATED_RESOURCE_HEADERS})
    add_dependencies(reaper_csurf_launchpadpro compile_resources)

    #TODO - is swell only for non WIN?
    add_definitions(-DSWELL_PROVIDED_BY_APP)
else()
endif()


target_link_libraries(reaper_csurf_launchpadpro ${LINKED_LIBRARIES})

set_target_properties(reaper_csurf_launchpadpro PROPERTIES VERSION ${PROJECT_VERSION})