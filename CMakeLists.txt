cmake_minimum_required(VERSION 3.20)

project(raytracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(raytracer
    src/main.cpp
)

add_executable(raytracer_bench src/benchmark.cpp)
add_executable(raytracer_bench_rendering src/benchmark_rendering.cpp)
add_executable(raytracer_profile_rendering src/profile_rendering.cpp)
add_executable(raytracer_diagnostic src/diagnostic.cpp)
add_executable(raytracer_construction src/construction.cpp)
target_compile_definitions(raytracer_diagnostic PRIVATE RAYTRACER_TRAVERSAL_STATS=1)

# Preserve walkable stacks only for the dedicated perf executable; benchmark
# targets retain their original optimized code generation.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(raytracer_profile_rendering PRIVATE
        -fno-omit-frame-pointer
        -fno-optimize-sibling-calls
    )
endif()

foreach(target IN ITEMS raytracer raytracer_bench raytracer_bench_rendering raytracer_profile_rendering raytracer_diagnostic raytracer_construction)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_include_directories(${target} SYSTEM PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pcg-cpp/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/happly"
    )
endforeach()
