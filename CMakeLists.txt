cmake_minimum_required(VERSION 3.20)

project(slang-rhi)

include(CMakeDependentOption)


option(SLANG_RHI_BUILD_SHARED "Build shared library" OFF)
option(SLANG_RHI_BUILD_TESTS "Build tests" ON)

set(SLANG_RHI_HAS_D3D12 ${WIN32})
cmake_dependent_option(SLANG_RHI_ENABLE_D3D12 "Enable D3D12 backend" ON "SLANG_RHI_HAS_D3D12" OFF)
set(SLANG_RHI_HAS_VULKAN ON)
cmake_dependent_option(SLANG_RHI_ENABLE_VULKAN "Enable Vulkan backend" ON "SLANG_RHI_HAS_VULKAN" OFF)
set(SLANG_RHI_HAS_METAL ${APPLE})
cmake_dependent_option(SLANG_RHI_ENABLE_METAL "Enable Metal backend" ON "SLANG_RHI_HAS_METAL" OFF)
set(SLANG_RHI_HAS_CUDA ON)
cmake_dependent_option(SLANG_RHI_ENABLE_CUDA "Enable CUDA backend" ON "SLANG_RHI_HAS_CUDA" OFF)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-assume -Wno-switch")
endif()

set(SLANG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../slang/include)
set(SLANG_BINARY_DIR ${CMAKE_SOURCE_DIR}/../slang/build/Debug)

if(CMAKE_CONFIGURATION_TYPES)
    set(SLANG_RHI_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>)
else()
    set(SLANG_RHI_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

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

if(SLANG_RHI_BUILD_SHARED)
    add_library(slang-rhi SHARED)
else()
    add_library(slang-rhi STATIC)
endif()

file(GLOB RHI_SOURCES 
    src/gfx/*.cpp
    src/gfx/cpu/*.cpp
    src/gfx/debug-layer/*.cpp
    src/utils/*.cpp
)
target_sources(slang-rhi PRIVATE ${RHI_SOURCES})

if(SLANG_RHI_ENABLE_D3D12)
    file(GLOB D3D12_SOURCES src/gfx/d3d/*.cpp src/gfx/d3d12/*.cpp src/gfx/nvapi/*.cpp)
    target_sources(slang-rhi PRIVATE ${D3D12_SOURCES})
endif()
if(SLANG_RHI_ENABLE_VULKAN)
    file(GLOB VULKAN_SOURCES src/gfx/vulkan/*.cpp)
    target_sources(slang-rhi PRIVATE ${VULKAN_SOURCES})

    add_subdirectory(external/vulkan-headers)
    target_link_libraries(slang-rhi PRIVATE Vulkan::Headers)
endif()
if(SLANG_RHI_ENABLE_METAL)
    file(GLOB METAL_SOURCES src/gfx/apple/*.mm src/gfx/metal/*.cpp)
    target_sources(slang-rhi PRIVATE ${METAL_SOURCES})

    add_library(metal-cpp INTERFACE)
    target_include_directories(metal-cpp INTERFACE external/metal-cpp)
    target_link_libraries(metal-cpp INTERFACE "-framework Foundation" "-framework Metal" "-framework QuartzCore")
    target_link_libraries(slang-rhi PRIVATE metal-cpp)
endif()
if(SLANG_RHI_ENABLE_CUDA)
    file(GLOB CUDA_SOURCES src/gfx/cuda/*.cpp)
    target_sources(slang-rhi PRIVATE ${CUDA_SOURCES})
endif()

if(WIN32)
    target_sources(slang-rhi PRIVATE ${D3D12_SOURCES})
    target_sources(slang-rhi PRIVATE ${VULKAN_SOURCES})
    add_custom_command(
        OUTPUT ${SLANG_RHI_OUTPUT_DIRECTORY}/slang.dll
        DEPENDS ${SLANG_BINARY_DIR}/bin/slang.dll
        COMMAND ${CMAKE_COMMAND} -E copy ${SLANG_BINARY_DIR}/bin/slang.dll ${SLANG_RHI_OUTPUT_DIRECTORY}/slang.dll
        COMMENT "Copying slang.dll"
    )
    add_custom_target(slang_binary_files ALL DEPENDS ${SLANG_RHI_OUTPUT_DIRECTORY}/slang.dll)
    add_dependencies(slang-rhi slang_binary_files)    
endif()

target_include_directories(slang-rhi PUBLIC include)
target_include_directories(slang-rhi PRIVATE src)
target_compile_definitions(slang-rhi
    PRIVATE
        SLANG_RHI_ENABLE_D3D12=$<BOOL:${SLANG_RHI_ENABLE_D3D12}>
        SLANG_RHI_ENABLE_VULKAN=$<BOOL:${SLANG_RHI_ENABLE_VULKAN}>
        SLANG_RHI_ENABLE_METAL=$<BOOL:${SLANG_RHI_ENABLE_METAL}>
        SLANG_RHI_ENABLE_CUDA=$<BOOL:${SLANG_RHI_ENABLE_CUDA}>
        $<$<PLATFORM_ID:Windows>:NOMINMAX>  # do not define min/max macros
        $<$<PLATFORM_ID:Windows>:UNICODE>   # force character map to unicode
)
target_compile_features(slang-rhi PRIVATE cxx_std_20)
target_link_libraries(slang-rhi PRIVATE slang)

if(SLANG_RHI_BUILD_TESTS)
    add_library(doctest INTERFACE)
    target_include_directories(doctest INTERFACE external/doctest)

    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE external/stb)
    
    file(GLOB TEST_SOURCES
        tests/*.cpp
    )
    add_executable(slang-rhi-tests)
    target_sources(slang-rhi-tests PRIVATE ${TEST_SOURCES})
    target_compile_definitions(slang-rhi-tests
        PRIVATE
            $<$<PLATFORM_ID:Windows>:NOMINMAX>  # do not define min/max macros
            $<$<PLATFORM_ID:Windows>:UNICODE>   # force character map to unicode
    )
    target_compile_features(slang-rhi-tests PRIVATE cxx_std_20)
    target_include_directories(slang-rhi-tests PRIVATE tests)
    target_link_libraries(slang-rhi-tests PRIVATE doctest stb slang slang-rhi)
endif()
