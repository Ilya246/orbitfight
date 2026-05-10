cmake_minimum_required(VERSION 3.20)
project(orbitfight VERSION 1.0.0 LANGUAGES CXX)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BUILD_SHARED_LIBS OFF)

include(FetchContent)

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

target_compile_options(orbitfight PRIVATE -g)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(orbitfight PRIVATE -Wall -Wextra -pedantic)
endif()

if(WIN32 OR MINGW)
    target_link_options(orbitfight PRIVATE -static)
endif()

set(SFML_USE_SYSTEM_DEPS OFF)
set(BUILD_SHARED_LIBS OFF)
set(SFML_STATIC_LIBRARIES ON)
set(SFML_VERSION 3.1.0)

# Find SFML3 specifically
find_package(SFML ${SFML_VERSION} COMPONENTS System Window Graphics Network QUIET)

if(NOT SFML_FOUND)
    message(STATUS "Couldn't find SFML, fetching.")
    FetchContent_Declare(
        SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG ${SFML_VERSION}
        GIT_SHALLOW 1
    )
    FetchContent_MakeAvailable(SFML)
endif()

target_link_libraries(orbitfight PRIVATE SFML::Graphics SFML::Window SFML::Network SFML::System)
