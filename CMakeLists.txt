cmake_minimum_required(VERSION 3.20)

project(slang-gfx)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()

add_subdirectory(external/vulkan-headers)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE external/stb)

set(SLANG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../slang/include)
set(SLANG_BINARY_DIR ${CMAKE_SOURCE_DIR}/../slang/build/Debug)

add_library(slang SHARED IMPORTED GLOBAL)
set_target_properties(slang PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${SLANG_INCLUDE_DIR}
    IMPORTED_IMPLIB ${SLANG_BINARY_DIR}/lib/slang.lib
    IMPORTED_LOCATION ${SLANG_BINARY_DIR}/bin/slang.dll
)
# sgl_copy_binary(${SLANG_DIR}/bin/slang.dll .)
# sgl_copy_binary(${SLANG_DIR}/bin/slang-glslang.dll .)
# sgl_copy_binary(${SLANG_DIR}/bin/slang-llvm.dll .)
# sgl_copy_binary(${SLANG_DIR}/bin/slang-rt.dll .)


file(GLOB CORE_SOURCES src/core/*.cpp)
add_library(slang-core STATIC)
target_sources(slang-core PRIVATE ${CORE_SOURCES})
target_include_directories(slang-core PUBLIC ${SLANG_INCLUDE_DIR} src)
target_compile_definitions(slang-core
    PRIVATE
        $<$<PLATFORM_ID:Windows>:NOMINMAX>  # do not define min/max macros
        $<$<PLATFORM_ID:Windows>:UNICODE>   # force character map to unicode
)
target_compile_features(slang-core PRIVATE cxx_std_20)

file(GLOB GFX_SOURCES 
    src/gfx/*.cpp
    src/gfx/cpu/*.cpp
    src/gfx/cuda/*.cpp
    src/gfx/vulkan/*.cpp
    src/gfx/d3d/*.cpp
    src/gfx/d3d12/*.cpp
    src/gfx/nvapi/*.cpp
    src/gfx/debug-layer/*.cpp
)
add_library(slang-gfx STATIC)
target_sources(slang-gfx PRIVATE ${GFX_SOURCES})
target_compile_definitions(slang-gfx
    PRIVATE
        $<$<PLATFORM_ID:Windows>:NOMINMAX>  # do not define min/max macros
        $<$<PLATFORM_ID:Windows>:UNICODE>   # force character map to unicode
)
target_compile_features(slang-gfx PRIVATE cxx_std_20)
target_link_libraries(slang-gfx PRIVATE slang-core Vulkan::Headers slang)

file(GLOB TEST_SOURCES
    src/tests/*.cpp
    src/unit-test/*.cpp
    src/gfx-util/*.cpp
)
add_executable(slang-gfx-tests)
target_sources(slang-gfx-tests PRIVATE ${TEST_SOURCES})
target_compile_definitions(slang-gfx-tests
    PRIVATE
        $<$<PLATFORM_ID:Windows>:NOMINMAX>  # do not define min/max macros
        $<$<PLATFORM_ID:Windows>:UNICODE>   # force character map to unicode
)
target_compile_features(slang-gfx-tests PRIVATE cxx_std_20)
target_include_directories(slang-gfx-tests PRIVATE src)
target_link_libraries(slang-gfx-tests PRIVATE slang-core slang-gfx stb)
