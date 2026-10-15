cmake_minimum_required(VERSION 3.20)

project(raytracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(raytracer
    src/main.cpp
)

add_executable(raytracer_bench src/benchmark.cpp)

target_include_directories(raytracer_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(raytracer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
