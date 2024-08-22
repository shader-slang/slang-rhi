cmake_minimum_required(VERSION 3.20)

project(slang-gfx)

file(GLOB FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/gfx/**/*.cpp)
add_library(slang-gfx STATIC)
target_sources(slang-gfx PRIVATE ${FILES})
target_compile_options(slang-gfx PRIVATE cxx_std_17)
