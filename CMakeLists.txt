cmake_minimum_required(VERSION 3.20)
project(orbitfight VERSION 1.0.0 LANGUAGES CXX)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BUILD_SHARED_LIBS OFF)

# Default to Release with Debug info (-O3 -g) if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(orbitfight ${SOURCES})

target_include_directories(orbitfight
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_options(orbitfight PRIVATE -g)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(orbitfight PRIVATE -Wall -Wextra -pedantic)
endif()

# Static linking — no external shared library dependencies
# The server uses POSIX sockets + std::thread + OpenSSL (for optional TLS).
target_link_options(orbitfight PRIVATE -static)

# Link pthread for std::thread / std::async
find_package(Threads REQUIRED)
target_link_libraries(orbitfight PRIVATE Threads::Threads)

# Link OpenSSL for TLS (wss://) support
set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)
target_link_libraries(orbitfight PRIVATE OpenSSL::SSL OpenSSL::Crypto ${CMAKE_DL_LIBS})
