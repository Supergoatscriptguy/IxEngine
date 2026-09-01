cmake_minimum_required(VERSION 3.15)
project(ixchess_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

file(GLOB ENGINE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# The default net is compiled into the binary.
set(EMBED_NET ${CMAKE_CURRENT_SOURCE_DIR}/nets/ix-gen2.nnue CACHE FILEPATH "net to embed")
set(NET_DATA_CPP ${CMAKE_CURRENT_BINARY_DIR}/net_data.cpp)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(
    OUTPUT ${NET_DATA_CPP}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_net.py ${EMBED_NET} ${NET_DATA_CPP}
    DEPENDS ${EMBED_NET} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_net.py
    COMMENT "Embedding ${EMBED_NET}")

add_executable(ixchess-engine ${ENGINE_SOURCES} ${NET_DATA_CPP})

find_package(Threads REQUIRED)
target_link_libraries(ixchess-engine PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(ixchess-engine PRIVATE /O2 /EHsc /permissive- /wd4146 /arch:AVX2 /GL)
    target_link_options(ixchess-engine PRIVATE /LTCG)
    # /wd4146: unary minus on unsigned is intentional in bit tricks.
    # /arch:AVX2: target modern x86-64 (auto-vectorizes the NNUE int16 loops).
    # /GL + /LTCG: whole-program optimization (cross-TU inlining).

    # Profile-guided optimization: build_pgo.bat drives GEN -> train -> USE.
    set(PGO "" CACHE STRING "MSVC PGO phase: GEN or USE (empty = off)")
    if(PGO STREQUAL "GEN")
        target_link_options(ixchess-engine PRIVATE /GENPROFILE)
    elseif(PGO STREQUAL "USE")
        target_link_options(ixchess-engine PRIVATE /USEPROFILE)
    endif()
else()
    target_compile_options(ixchess-engine PRIVATE -O3 -funroll-loops -Wall -Wno-unused-parameter)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i.86")
        target_compile_options(ixchess-engine PRIVATE -mavx2 -mpopcnt)
    endif()
endif()

# Make the binary easy for the Python tools to find.
set_target_properties(ixchess-engine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
