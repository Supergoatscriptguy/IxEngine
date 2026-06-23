cmake_minimum_required(VERSION 3.15)
project(ixchess_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

file(GLOB ENGINE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(ixchess-engine ${ENGINE_SOURCES})

find_package(Threads REQUIRED)
target_link_libraries(ixchess-engine PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(ixchess-engine PRIVATE /O2 /EHsc /permissive- /wd4146)
    # /wd4146: unary minus on unsigned is intentional in bit tricks.
else()
    target_compile_options(ixchess-engine PRIVATE
        -O3 -funroll-loops -mpopcnt -Wall -Wno-unused-parameter)
endif()

# Make the binary easy for the Python tools to find.
set_target_properties(ixchess-engine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
