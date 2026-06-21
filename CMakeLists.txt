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

set(ixwebsocket_VERSION 12.0.0)
# IXWebSocket options: TLS via OpenSSL (matches existing dependency),
# static lib, no test/demo/internal-http-server features.
set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
set(USE_MBED_TLS OFF CACHE BOOL "" FORCE)
set(USE_SECURE_TRANSPORT OFF CACHE BOOL "" FORCE)
set(USE_ZLIB ON CACHE BOOL "" FORCE)
set(BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
# Force IXWebSocket's find_package(OpenSSL) to grab the static libs too,
# so our final `-static` link doesn't trip on libssl.so.
set(OPENSSL_USE_STATIC_LIBS TRUE CACHE BOOL "" FORCE)
find_package(ixwebsocket)
if(NOT ixwebsocket_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        ixwebsocket
        GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
        GIT_TAG        v${ixwebsocket_VERSION}
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(ixwebsocket)
endif()

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(orbitfight ${SOURCES})

if(DEFINED ixwebsocket_SOURCE_DIR)
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${ixwebsocket_SOURCE_DIR}/include)
endif()

target_include_directories(orbitfight
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_options(orbitfight PRIVATE -g)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(orbitfight PRIVATE -Wall -Wextra -pedantic)
endif()

target_link_options(orbitfight PRIVATE -static)

# Link pthread for std::thread / std::async
find_package(Threads REQUIRED)
target_link_libraries(orbitfight PRIVATE Threads::Threads)

# IXWebSocket + its OpenSSL backend (for wss://) support
target_link_libraries(orbitfight PRIVATE ixwebsocket)
find_package(OpenSSL REQUIRED)
target_link_libraries(orbitfight PRIVATE OpenSSL::SSL OpenSSL::Crypto ${CMAKE_DL_LIBS})
