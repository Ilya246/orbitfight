cmake_minimum_required(VERSION 3.20)
project(orbitfight VERSION 1.0.0 LANGUAGES CXX)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release with Debug info (-O3 -g) if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Find host system's xxd to pack assets
find_program(XXD_EXECUTABLE xxd REQUIRED)

# Actually pack assets
file(GLOB_RECURSE ASSET_FILES "assets/*")
set(ASSET_HEADERS "")
set(ASSET_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
file(MAKE_DIRECTORY "${ASSET_OUT_DIR}")

foreach(ASSET_PATH IN LISTS ASSET_FILES)
    if(NOT IS_DIRECTORY "${ASSET_PATH}")
        get_filename_component(ASSET_BASENAME "${ASSET_PATH}" NAME_WLE)
        set(ASSET_OUTPUT "${ASSET_OUT_DIR}/${ASSET_BASENAME}.asset")

        file(RELATIVE_PATH ASSET_REL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${ASSET_PATH}")

        add_custom_command(
            OUTPUT "${ASSET_OUTPUT}"
            COMMAND ${XXD_EXECUTABLE} -i "${ASSET_REL_PATH}" "${ASSET_OUTPUT}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            DEPENDS "${ASSET_PATH}"
            COMMENT "Packing ${ASSET_REL_PATH} -> ${ASSET_OUTPUT}"
        )
        list(APPEND ASSET_HEADERS "${ASSET_OUTPUT}")
    endif()
endforeach()

file(GLOB_RECURSE SOURCES "src/*.cpp")

# Maybe add WIN32 later to not show console window?
add_executable(orbitfight ${SOURCES} ${ASSET_HEADERS})

target_include_directories(orbitfight
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${ASSET_OUT_DIR}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(orbitfight PRIVATE -Wall -Wextra -pedantic)
endif()

if(WIN32 OR MINGW)
    target_link_options(orbitfight PRIVATE -static)
endif()

set(SFML_STATIC_LIBRARIES ON)

# Find SFML3 specifically
find_package(SFML 3 COMPONENTS System Window Graphics Network QUIET)

if(SFML_FOUND)
    message(STATUS "Using find_package SFML.")

    # Cursed OS case-sensitivity mismatch patch.
    if(CMAKE_CROSSCOMPILING AND MINGW)
        foreach(SFML_TGT SFML::System SFML::Window SFML::Graphics SFML::Network)
            if(TARGET ${SFML_TGT})
                foreach(PROP INTERFACE_LINK_LIBRARIES IMPORTED_LINK_INTERFACE_LIBRARIES IMPORTED_LINK_INTERFACE_LIBRARIES_RELEASE IMPORTED_LINK_INTERFACE_LIBRARIES_DEBUG)
                    get_target_property(IL ${SFML_TGT} ${PROP})
                    if(IL)
                        string(REPLACE "Crypt32" "crypt32" IL "${IL}")
                        string(REPLACE "Dnsapi" "dnsapi" IL "${IL}")
                        string(REPLACE "Ws2_32" "ws2_32" IL "${IL}")
                        string(REPLACE "Winmm" "winmm" IL "${IL}")
                        string(REPLACE "OpenGL32" "opengl32" IL "${IL}")
                        string(REPLACE "Opengl32" "opengl32" IL "${IL}")
                        string(REPLACE "Gdi32" "gdi32" IL "${IL}")
                        string(REPLACE "Advapi32" "advapi32" IL "${IL}")
                        string(REPLACE "User32" "user32" IL "${IL}")
                        set_target_properties(${SFML_TGT} PROPERTIES ${PROP} "${IL}")
                    endif()
                endforeach()
            endif()
        endforeach()
    endif()

    target_link_libraries(orbitfight PRIVATE SFML::Graphics SFML::Window SFML::Network SFML::System)
elseif(NOT CMAKE_CROSSCOMPILING)
    # None, hope we're on Linux and try pkg_config
    message(STATUS "Couldn't find SFML, trying pkg_config. Install SFML or fix its installation if this fails.")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SFML REQUIRED IMPORTED_TARGET sfml-window sfml-graphics sfml-system sfml-network)
    target_link_libraries(orbitfight PRIVATE PkgConfig::SFML)
else()
    message(FATAL_ERROR "SFML not found for cross-compilation! CMAKE_PREFIX_PATH must point to SFML.")
endif()
