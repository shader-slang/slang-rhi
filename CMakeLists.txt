cmake_minimum_required(VERSION 3.20)

project(slang-rhi)

include(CMakeDependentOption)

# Check if this project is the master cmake project (i.e. not included via add_subdirectory).
set(SLANG_RHI_MASTER_PROJECT OFF)
if (${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_SOURCE_DIR})
  set(SLANG_RHI_MASTER_PROJECT ON)
endif()

option(SLANG_RHI_BUILD_SHARED "Build shared library" OFF)
option(SLANG_RHI_BUILD_TESTS "Build tests" ON)

set(SLANG_RHI_HAS_D3D11 ${WIN32})
cmake_dependent_option(SLANG_RHI_ENABLE_D3D11 "Enable D3D11 backend" ON "SLANG_RHI_HAS_D3D11" OFF)
set(SLANG_RHI_HAS_D3D12 ${WIN32})
cmake_dependent_option(SLANG_RHI_ENABLE_D3D12 "Enable D3D12 backend" ON "SLANG_RHI_HAS_D3D12" OFF)
set(SLANG_RHI_HAS_VULKAN ON)
cmake_dependent_option(SLANG_RHI_ENABLE_VULKAN "Enable Vulkan backend" ON "SLANG_RHI_HAS_VULKAN" OFF)
set(SLANG_RHI_HAS_METAL ${APPLE})
cmake_dependent_option(SLANG_RHI_ENABLE_METAL "Enable Metal backend" ON "SLANG_RHI_HAS_METAL" OFF)
set(SLANG_RHI_HAS_CUDA ON)
if(APPLE)
    set(SLANG_RHI_HAS_CUDA OFF)
endif()
cmake_dependent_option(SLANG_RHI_ENABLE_CUDA "Enable CUDA backend" ON "SLANG_RHI_HAS_CUDA" OFF)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()

if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-assume -Wno-switch")
endif()

set(SLANG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../slang/include CACHE STRING "Slang include directory")
set(SLANG_BINARY_DIR ${CMAKE_SOURCE_DIR}/../slang/build/Debug CACHE STRING "Slang binary directory")

if(CMAKE_CONFIGURATION_TYPES)
    set(SLANG_RHI_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>)
else()
    set(SLANG_RHI_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

if(SLANG_RHI_BUILD_SHARED)
    add_library(slang-rhi SHARED)
else()
    add_library(slang-rhi STATIC)
endif()

if(SLANG_RHI_MASTER_PROJECT)
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

    if(WIN32)
        add_custom_command(
            OUTPUT ${SLANG_RHI_OUTPUT_DIRECTORY}/slang.dll
            DEPENDS ${SLANG_BINARY_DIR}/bin/slang.dll
            COMMAND ${CMAKE_COMMAND} -E copy ${SLANG_BINARY_DIR}/bin/slang.dll ${SLANG_RHI_OUTPUT_DIRECTORY}/slang.dll
            COMMENT "Copying slang.dll"
        )
        add_custom_target(slang_binary_files ALL DEPENDS ${SLANG_RHI_OUTPUT_DIRECTORY}/slang.dll)
        add_dependencies(slang-rhi slang_binary_files)
    endif()

    target_link_libraries(slang-rhi PRIVATE slang)

    # sgl_copy_binary(${SLANG_DIR}/bin/slang.dll .)
    # sgl_copy_binary(${SLANG_DIR}/bin/slang-glslang.dll .)
    # sgl_copy_binary(${SLANG_DIR}/bin/slang-llvm.dll .)
    # sgl_copy_binary(${SLANG_DIR}/bin/slang-rt.dll .)
else()
    target_include_directories(slang-rhi PUBLIC ${SLANG_INCLUDE_DIR})
endif()

file(GLOB RHI_SOURCES
    src/*.cpp
    src/core/*.cpp
    src/cpu/*.cpp
    src/debug-layer/*.cpp
)
target_sources(slang-rhi PRIVATE ${RHI_SOURCES})

if(APPLE)
    file(GLOB OBJC_SOURCES src/*.mm)
    target_sources(slang-rhi PRIVATE ${OBJC_SOURCES})
endif()
if(SLANG_RHI_ENABLE_D3D11 OR SLANG_RHI_HAS_D3D12)
    file(GLOB D3D_SOURCES src/d3d/*.cpp src/nvapi/*.cpp)
    target_sources(slang-rhi PRIVATE ${D3D_SOURCES})
endif()
if(SLANG_RHI_ENABLE_D3D11)
        file(GLOB D3D11_SOURCES src/d3d11/*.cpp)
    target_sources(slang-rhi PRIVATE ${D3D11_SOURCES})
endif()
if(SLANG_RHI_ENABLE_D3D12)
    file(GLOB D3D12_SOURCES src/d3d12/*.cpp)
    target_sources(slang-rhi PRIVATE ${D3D12_SOURCES})
endif()
if(SLANG_RHI_ENABLE_VULKAN)
    file(GLOB VULKAN_SOURCES src/vulkan/*.cpp)
    target_sources(slang-rhi PRIVATE ${VULKAN_SOURCES})

    add_library(slang-rhi-vulkan-headers INTERFACE)
    target_include_directories(slang-rhi-vulkan-headers INTERFACE external/vulkan-headers/include)
    target_link_libraries(slang-rhi PRIVATE slang-rhi-vulkan-headers)
endif()
if(SLANG_RHI_ENABLE_METAL)
    file(GLOB METAL_SOURCES src/metal/*.cpp src/metal/*.mm)
    target_sources(slang-rhi PRIVATE ${METAL_SOURCES})

    add_library(slang-rhi-metal-cpp INTERFACE)
    target_include_directories(slang-rhi-metal-cpp INTERFACE external/metal-cpp)
    target_link_libraries(slang-rhi-metal-cpp INTERFACE "-framework Foundation" "-framework Metal" "-framework QuartzCore")
    target_link_libraries(slang-rhi PRIVATE slang-rhi-metal-cpp)
endif()
if(SLANG_RHI_ENABLE_CUDA)
    file(GLOB CUDA_SOURCES src/cuda/*.cpp)
    target_sources(slang-rhi PRIVATE ${CUDA_SOURCES})
endif()

target_include_directories(slang-rhi PUBLIC include)
target_include_directories(slang-rhi PRIVATE src)
target_compile_definitions(slang-rhi
    PRIVATE
        SLANG_RHI_ENABLE_D3D11=$<BOOL:${SLANG_RHI_ENABLE_D3D11}>
        SLANG_RHI_ENABLE_D3D12=$<BOOL:${SLANG_RHI_ENABLE_D3D12}>
        SLANG_RHI_ENABLE_VULKAN=$<BOOL:${SLANG_RHI_ENABLE_VULKAN}>
        SLANG_RHI_ENABLE_METAL=$<BOOL:${SLANG_RHI_ENABLE_METAL}>
        SLANG_RHI_ENABLE_CUDA=$<BOOL:${SLANG_RHI_ENABLE_CUDA}>
        $<$<PLATFORM_ID:Windows>:NOMINMAX>  # do not define min/max macros
        $<$<PLATFORM_ID:Windows>:UNICODE>   # force character map to unicode
)
target_compile_features(slang-rhi PRIVATE cxx_std_17)
set_target_properties(slang-rhi PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
    target_compile_features(slang-rhi-tests PRIVATE cxx_std_17)
    target_include_directories(slang-rhi-tests PRIVATE tests)
    target_link_libraries(slang-rhi-tests PRIVATE doctest stb slang slang-rhi)
endif()
