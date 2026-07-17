cmake_minimum_required(VERSION 3.25)
project(UltraCBF VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

find_package(Geode REQUIRED)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/SubTickEngine.cpp
    src/Hooks.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /O2 /Oi /Ot)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -O3 -ffast-math)
endif()

setup_geode_mod(${PROJECT_NAME})