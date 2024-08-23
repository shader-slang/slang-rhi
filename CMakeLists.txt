cmake_minimum_required(VERSION 3.20)

project(slang-rhi)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-assume -Wno-switch")
endif()

add_subdirectory(external/vulkan-headers)

add_library(metal-cpp INTERFACE)
target_include_directories(metal-cpp INTERFACE external/metal-cpp)
target_link_libraries(metal-cpp INTERFACE "-framework Foundation" "-framework Metal" "-framework QuartzCore")

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE external/stb)

set(SLANG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../slang/include)
set(SLANG_BINARY_DIR ${CMAKE_SOURCE_DIR}/../slang/build/Debug)

add_library(slang SHARED IMPORTED GLOBAL)
if(WIN32)
    set_target_properties(slang PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${SLANG_INCLUDE_DIR}
        IMPORTED_IMPLIB ${SLANG_BINARY_DIR}/lib/slang.lib
        IMPORTED_LOCATION ${SLANG_BINARY_DIR}/bin/slang.dll
    )
elseif(APPLE)
    set_target_properties(slang PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${SLANG_INCLUDE_DIR}
        IMPORTED_LOCATION ${SLANG_BINARY_DIR}/lib/libslang.dylib
    )
endif()
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

file(GLOB RHI_SOURCES 
    src/gfx/*.cpp
    src/gfx/apple/*.mm
    src/gfx/cpu/*.cpp
    src/gfx/cuda/*.cpp
    src/gfx/debug-layer/*.cpp
    src/gfx/vulkan/*.cpp
    $<$<PLATFORM_ID:Windows>:src/gfx/d3d/*.cpp>
    $<$<PLATFORM_ID:Windows>:src/gfx/d3d12/*.cpp>
    src/gfx/metal/*.cpp
    src/gfx/nvapi/*.cpp
)
add_library(slang-rhi STATIC)
target_sources(slang-rhi PRIVATE ${RHI_SOURCES})
target_include_directories(slang-rhi PUBLIC include)
target_compile_definitions(slang-rhi
    PRIVATE
        $<$<PLATFORM_ID:Windows>:NOMINMAX>  # do not define min/max macros
        $<$<PLATFORM_ID:Windows>:UNICODE>   # force character map to unicode
)
target_compile_features(slang-rhi PRIVATE cxx_std_20)
target_link_libraries(slang-rhi
    PRIVATE
        slang
        slang-core
        Vulkan::Headers
        $<$<PLATFORM_ID:Darwin>:metal-cpp>
)

file(GLOB TEST_SOURCES
    src/tests/*.cpp
    src/unit-test/*.cpp
    src/gfx-util/*.cpp
)
add_executable(slang-rhi-tests)
target_sources(slang-rhi-tests PRIVATE ${TEST_SOURCES})
target_compile_definitions(slang-rhi-tests
    PRIVATE
        $<$<PLATFORM_ID:Windows>:NOMINMAX>  # do not define min/max macros
        $<$<PLATFORM_ID:Windows>:UNICODE>   # force character map to unicode
)
target_compile_features(slang-rhi-tests PRIVATE cxx_std_20)
target_include_directories(slang-rhi-tests PRIVATE src)
target_link_libraries(slang-rhi-tests PRIVATE slang-core slang-rhi stb)
