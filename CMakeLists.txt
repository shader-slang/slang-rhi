cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0177)
cmake_policy(SET CMP0177 NEW)
endif()

project(slang-rhi)

# Avoid warning about DOWNLOAD_EXTRACT_TIMESTAMP in CMake 3.24:
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif()

# Add the cmake directory to the module path.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(CMakeDependentOption)
include(FetchPackage)
include(DetermineTargetArchitecture)
include(GNUInstallDirs)
include(CMakeRC)

# Determine the target architecture we build for.
# CMAKE_SYSTEM_PROCESSOR is not a reliable way to determine the target architecture.
determine_target_architecture(SLANG_RHI_ARCHITECTURE)

# set(FETCHCONTENT_UPDATES_DISCONNECTED OFF)

# Check if this project is the master cmake project (i.e. not included via add_subdirectory).
if(${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_SOURCE_DIR})
    set(SLANG_RHI_MASTER_PROJECT ON)
else()
    set(SLANG_RHI_MASTER_PROJECT OFF)
endif()

if(NOT DEFINED SLANG_RHI_BINARY_DIR)
    if(CMAKE_CONFIGURATION_TYPES)
        set(SLANG_RHI_BINARY_DIR ${CMAKE_BINARY_DIR}/$<CONFIG>)
    else()
        set(SLANG_RHI_BINARY_DIR ${CMAKE_BINARY_DIR})
    endif()
endif()

# Configuration options
option(SLANG_RHI_BUILD_SHARED "Build shared library" OFF)
option(SLANG_RHI_BUILD_TESTS "Build tests" ${SLANG_RHI_MASTER_PROJECT})
option(SLANG_RHI_BUILD_EXAMPLES "Build examples" ${SLANG_RHI_MASTER_PROJECT})
option(SLANG_RHI_ENABLE_COVERAGE "Enable code coverage (clang only)" OFF)
option(SLANG_RHI_INSTALL "Install library" ON)

# Configure coverage flags
if(SLANG_RHI_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
        add_link_options(-fprofile-instr-generate)
    endif()
endif()

# Determine available backends
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(SLANG_RHI_HAS_D3D11 ON)
    set(SLANG_RHI_HAS_D3D12 ON)
    set(SLANG_RHI_HAS_VULKAN ON)
    set(SLANG_RHI_HAS_METAL OFF)
    set(SLANG_RHI_HAS_CUDA ON)
    set(SLANG_RHI_HAS_WGPU ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SLANG_RHI_HAS_D3D11 OFF)
    set(SLANG_RHI_HAS_D3D12 OFF)
    set(SLANG_RHI_HAS_VULKAN ON)
    set(SLANG_RHI_HAS_METAL OFF)
    set(SLANG_RHI_HAS_CUDA ON)
    set(SLANG_RHI_HAS_WGPU ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(SLANG_RHI_HAS_D3D11 OFF)
    set(SLANG_RHI_HAS_D3D12 OFF)
    set(SLANG_RHI_HAS_VULKAN ON)
    set(SLANG_RHI_HAS_METAL ON)
    set(SLANG_RHI_HAS_CUDA OFF)
    set(SLANG_RHI_HAS_WGPU ON)
endif()

set(SLANG_RHI_HAS_AGILITY_SDK OFF)
if(SLANG_RHI_HAS_D3D12 AND (CMAKE_SYSTEM_NAME STREQUAL "Windows"))
    set(SLANG_RHI_HAS_AGILITY_SDK ON)
endif()

set(SLANG_RHI_HAS_NVAPI OFF)
if((SLANG_RHI_HAS_D3D11 OR SLANG_RHI_HAS_D3D12) AND (CMAKE_SYSTEM_NAME STREQUAL "Windows") AND (SLANG_RHI_ARCHITECTURE MATCHES "x86_64") AND WIN32)
    set(SLANG_RHI_HAS_NVAPI ON)
endif()

# Backend options
option(SLANG_RHI_ENABLE_CPU "Enable CPU backend" ON)
cmake_dependent_option(SLANG_RHI_ENABLE_D3D11 "Enable D3D11 backend" ON "SLANG_RHI_HAS_D3D11" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_D3D12 "Enable D3D12 backend" ON "SLANG_RHI_HAS_D3D12" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_VULKAN "Enable Vulkan backend" ON "SLANG_RHI_HAS_VULKAN" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_METAL "Enable Metal backend" ON "SLANG_RHI_HAS_METAL" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_CUDA "Enable CUDA backend" ON "SLANG_RHI_HAS_CUDA" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_WGPU "Enable WebGPU backend" ON "SLANG_RHI_HAS_WGPU" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_AGILITY_SDK "Enable Agility SDK" ON "SLANG_RHI_HAS_AGILITY_SDK" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_NVAPI "Enable NVAPI" ON "SLANG_RHI_HAS_NVAPI" OFF)

# Fetch slang options
option(SLANG_RHI_FETCH_SLANG "Fetch slang" ON)
set(SLANG_RHI_FETCH_SLANG_VERSION "2025.7.1" CACHE STRING "Slang version to fetch")

# Fetch WinPixEventRuntime options
option(SLANG_RHI_FETCH_WINPIX "Fetch WinPixEventRuntime" ON)
set(SLANG_RHI_FETCH_WINPIX_URL "https://www.nuget.org/api/v2/package/WinPixEventRuntime/1.0.240308001" CACHE STRING "WinPixEventRuntime URL to fetch")

# Fetch dxc options
option(SLANG_RHI_FETCH_DXC "Fetch dxc (DirectX Shader Compiler)" ON)
set(SLANG_RHI_FETCH_DXC_URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31.zip" CACHE STRING "DXC URL to fetch")

# Fetch Agility SDK options
option(SLANG_RHI_FETCH_AGILITY_SDK "Fetch Agility SDK" ON)
set(SLANG_RHI_FETCH_AGILITY_SDK_VERSION "1.615.1" CACHE STRING "Agility SDK version to fetch")

# Fetch NVAPI options
option(SLANG_RHI_FETCH_NVAPI "Fetch NVAPI" ON)
set(SLANG_RHI_FETCH_NVAPI_URL "https://github.com/NVIDIA/nvapi/archive/d08488fcc82eef313b0464db37d2955709691e94.zip" CACHE STRING "NVAPI URL to fetch")

# Fetch OptiX options
option(SLANG_RHI_FETCH_OPTIX "Fetch OptiX" ON)
set(SLANG_RHI_FETCH_OPTIX_URL "https://developer.download.nvidia.com/redist/optix/v8.0/OptiX-8.0-Include.zip" CACHE STRING "OptiX URL to fetch")
# set(SLANG_RHI_FETCH_OPTIX_URL "https://developer.download.nvidia.com/redist/optix/v8.1/OptiX-8.1.0-Include.zip" CACHE STRING "OptiX URL to fetch")

# Fetch Dawn options
option(SLANG_RHI_FETCH_DAWN "Fetch Dawn" ON)
set(SLANG_RHI_FETCH_DAWN_VERSION "131.0.6738.0" CACHE STRING "Dawn version to fetch")


if(SLANG_RHI_BUILD_SHARED)
    add_library(slang-rhi SHARED)
else()
    add_library(slang-rhi STATIC)
endif()

set(SLANG_RHI_COPY_FILES "")

# Function to copy a file to the binary output directory
macro(copy_file IN_FILE OUT_DIR)
    if(EXISTS ${IN_FILE})
        get_filename_component(FILENAME ${IN_FILE} NAME)
        set(OUT_FILE_1 "${CMAKE_CURRENT_BINARY_DIR}/${OUT_DIR}/${FILENAME}")
        set(OUT_FILE_2 "${SLANG_RHI_BINARY_DIR}/${OUT_DIR}/${FILENAME}")

        if (UNIX)
            add_custom_command(
                OUTPUT ${OUT_FILE_2} DEPENDS ${IN_FILE} ${ARGN}
                COMMAND ${CMAKE_COMMAND} -E copy ${IN_FILE} ${OUT_FILE_2}
                COMMENT "Copying ${FILENAME}"
            )
            list(APPEND SLANG_RHI_COPY_FILES ${OUT_FILE_2})
        else()
            # add_custom_command does not support generator expressions in OUTPUT argument
            # make a dummy copy and then depend on that
            add_custom_command(
                OUTPUT ${OUT_FILE_1} DEPENDS ${IN_FILE} ${ARGN}
                COMMAND ${CMAKE_COMMAND} -E copy ${IN_FILE} ${OUT_FILE_1}
                COMMAND ${CMAKE_COMMAND} -E copy ${IN_FILE} ${OUT_FILE_2}
                COMMENT "Copying ${FILENAME}"
            )
            list(APPEND SLANG_RHI_COPY_FILES ${OUT_FILE_1})
        endif()

        if(SLANG_RHI_INSTALL)
            install(FILES ${IN_FILE} DESTINATION ${CMAKE_INSTALL_BINDIR}/${OUT_DIR})
        endif()
    endif()
endmacro()

# Fetch slang
if(SLANG_RHI_FETCH_SLANG)
    set(SLANG_VERSION ${SLANG_RHI_FETCH_SLANG_VERSION})
    set(SLANG_URL "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}")

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if(SLANG_RHI_ARCHITECTURE MATCHES "x86_64")
            set(SLANG_URL "${SLANG_URL}-windows-x86_64.zip")
        elseif(SLANG_RHI_ARCHITECTURE MATCHES "aarch64|arm64")
            set(SLANG_URL "${SLANG_URL}-windows-aarch64.zip")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if(SLANG_RHI_ARCHITECTURE MATCHES "x86_64")
            set(SLANG_URL "${SLANG_URL}-linux-x86_64-glibc-2.17.tar.gz")
        elseif(SLANG_RHI_ARCHITECTURE MATCHES "aarch64|arm64")
            set(SLANG_URL "${SLANG_URL}-linux-aarch64.tar.gz")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        if(CMAKE_APPLE_SILICON_PROCESSOR MATCHES "x86_64")
            set(SLANG_URL "${SLANG_URL}-macos-x86_64.zip")
        else()
            set(SLANG_URL "${SLANG_URL}-macos-aarch64.zip")
        endif()
    endif()

    message(STATUS "Fetching Slang ${SLANG_VERSION} ...")
    FetchPackage(slang URL ${SLANG_URL})
    set(SLANG_RHI_SLANG_INCLUDE_DIR ${slang_SOURCE_DIR}/include)
    set(SLANG_RHI_SLANG_BINARY_DIR ${slang_SOURCE_DIR})
endif()

# Setup slang
set(SLANG_RHI_SLANG_INCLUDE_DIR ${SLANG_RHI_SLANG_INCLUDE_DIR} CACHE STRING "Slang include directory")
set(SLANG_RHI_SLANG_BINARY_DIR ${SLANG_RHI_SLANG_BINARY_DIR} CACHE STRING "Slang binary directory")
unset(SLANG_RHI_SLANG_INCLUDE_DIR)
unset(SLANG_RHI_SLANG_BINARY_DIR)
if(NOT SLANG_RHI_BUILD_FROM_SLANG_REPO)
    add_library(slang SHARED IMPORTED GLOBAL)

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set_target_properties(slang PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES ${SLANG_RHI_SLANG_INCLUDE_DIR}
            IMPORTED_IMPLIB ${SLANG_RHI_SLANG_BINARY_DIR}/lib/slang.lib
            IMPORTED_LOCATION ${SLANG_RHI_SLANG_BINARY_DIR}/bin/slang.dll
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_target_properties(slang PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES ${SLANG_RHI_SLANG_INCLUDE_DIR}
            IMPORTED_LOCATION ${SLANG_RHI_SLANG_BINARY_DIR}/lib/libslang.so
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set_target_properties(slang PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES ${SLANG_RHI_SLANG_INCLUDE_DIR}
            IMPORTED_LOCATION ${SLANG_RHI_SLANG_BINARY_DIR}/lib/libslang.dylib
        )
    endif()

    target_link_libraries(slang-rhi PUBLIC slang)

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        copy_file(${SLANG_RHI_SLANG_BINARY_DIR}/bin/slang.dll .)
        copy_file(${SLANG_RHI_SLANG_BINARY_DIR}/bin/slang-glslang.dll .)
        copy_file(${SLANG_RHI_SLANG_BINARY_DIR}/bin/slang-llvm.dll .)
        copy_file(${SLANG_RHI_SLANG_BINARY_DIR}/bin/slang-rt.dll .)
    endif()
endif()

# Slang distributes prelude headers in the "include" directory along the other headers.
# However, in a slang build tree, the files are not in the "include" directory but in a separate "prelude" directory.
# Make sure to include the prelude directory if it exists.
target_include_directories(slang-rhi PUBLIC ${SLANG_RHI_SLANG_INCLUDE_DIR})
if(EXISTS ${SLANG_RHI_SLANG_INCLUDE_DIR}/../prelude)
    target_include_directories(slang-rhi PUBLIC ${SLANG_RHI_SLANG_INCLUDE_DIR}/../prelude)
endif()

# Fetch dxc
if(SLANG_RHI_FETCH_DXC AND SLANG_RHI_ENABLE_D3D12 AND (CMAKE_SYSTEM_NAME STREQUAL "Windows"))
    message(STATUS "Fetching DirectXShaderCompiler ...")
    FetchPackage(dxc URL ${SLANG_RHI_FETCH_DXC_URL})
    if(SLANG_RHI_ARCHITECTURE MATCHES "x86_64")
        copy_file(${dxc_SOURCE_DIR}/bin/x64/dxcompiler.dll .)
        copy_file(${dxc_SOURCE_DIR}/bin/x64/dxil.dll .)
    elseif(SLANG_RHI_ARCHITECTURE MATCHES "aarch64|arm64")
        copy_file(${dxc_SOURCE_DIR}/bin/arm64/dxcompiler.dll .)
        copy_file(${dxc_SOURCE_DIR}/bin/arm64/dxil.dll .)
    endif()
endif()

# Fetch WinPixEventRuntime
if(SLANG_RHI_FETCH_WINPIX AND SLANG_RHI_ENABLE_D3D12 AND (CMAKE_SYSTEM_NAME STREQUAL "Windows"))
    message(STATUS "Fetching WinPixEventRuntime ...")
    FetchPackage(winpix URL ${SLANG_RHI_FETCH_WINPIX_URL})
    if(SLANG_RHI_ARCHITECTURE MATCHES "x86_64")
        copy_file(${winpix_SOURCE_DIR}/bin/x64/WinPixEventRuntime.dll .)
    elseif(SLANG_RHI_ARCHITECTURE MATCHES "aarch64|arm64")
        copy_file(${winpix_SOURCE_DIR}/bin/arm64/WinPixEventRuntime.dll .)
    endif()
endif()

# Fetch Agility SDK
if(SLANG_RHI_FETCH_AGILITY_SDK AND SLANG_RHI_ENABLE_AGILITY_SDK)
    set(AGILITY_SDK_VERSION ${SLANG_RHI_FETCH_AGILITY_SDK_VERSION})
    message(STATUS "Fetching Agility SDK ${AGILITY_SDK_VERSION} ...")
    FetchPackage(agility_sdk URL "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/${AGILITY_SDK_VERSION}")
    set(SLANG_RHI_AGILITY_SDK_DIR ${agility_sdk_SOURCE_DIR})
endif()

# Setup Agility SDK
set(SLANG_RHI_AGILITY_SDK_DIR ${SLANG_RHI_AGILITY_SDK_DIR} CACHE STRING "Agility SDK directory")
unset(SLANG_RHI_AGILITY_SDK_DIR)
if (EXISTS "${SLANG_RHI_AGILITY_SDK_DIR}" AND SLANG_RHI_ENABLE_AGILITY_SDK)
    if(SLANG_RHI_ARCHITECTURE MATCHES "x86_64")
        copy_file(${SLANG_RHI_AGILITY_SDK_DIR}/build/native/bin/x64/D3D12Core.dll D3D12)
        copy_file(${SLANG_RHI_AGILITY_SDK_DIR}/build/native/bin/x64/d3d12SDKLayers.dll D3D12)
    elseif(SLANG_RHI_ARCHITECTURE MATCHES "aarch64|arm64")
        copy_file(${SLANG_RHI_AGILITY_SDK_DIR}/build/native/bin/arm64/D3D12Core.dll D3D12)
        copy_file(${SLANG_RHI_AGILITY_SDK_DIR}/build/native/bin/arm64/d3d12SDKLayers.dll D3D12)
    endif()
    add_library(slang-rhi-agility-sdk INTERFACE)
    target_include_directories(slang-rhi-agility-sdk INTERFACE ${SLANG_RHI_AGILITY_SDK_DIR}/build/native/include)
    target_link_libraries(slang-rhi PUBLIC slang-rhi-agility-sdk)
elseif(SLANG_RHI_ENABLE_AGILITY_SDK)
    message(FATAL_ERROR "Agility SDK not found! Either enable SLANG_RHI_FETCH_AGILITY_SDK or set SLANG_RHI_AGILITY_SDK_DIR to the Agility SDK directory.")
endif()

# Fetch NVAPI
if(SLANG_RHI_FETCH_NVAPI AND SLANG_RHI_ENABLE_NVAPI)
    message(STATUS "Fetching NVAPI ...")
    FetchPackage(nvapi URL ${SLANG_RHI_FETCH_NVAPI_URL})
    set(SLANG_RHI_NVAPI_DIR ${nvapi_SOURCE_DIR})
endif()

# Setup NVAPI
set(SLANG_RHI_NVAPI_DIR ${SLANG_RHI_NVAPI_DIR} CACHE STRING "NVAPI directory")
unset(SLANG_RHI_NVAPI_DIR)
if(EXISTS "${SLANG_RHI_NVAPI_DIR}" AND SLANG_RHI_ENABLE_NVAPI)
    add_library(slang-rhi-nvapi INTERFACE)
    target_include_directories(slang-rhi-nvapi INTERFACE ${SLANG_RHI_NVAPI_DIR})
    target_link_libraries(slang-rhi-nvapi INTERFACE ${SLANG_RHI_NVAPI_DIR}/amd64/nvapi64.lib)
    target_link_libraries(slang-rhi PRIVATE slang-rhi-nvapi)
    set(SLANG_RHI_NVAPI_INCLUDE_DIR ${SLANG_RHI_NVAPI_DIR})
elseif(SLANG_RHI_ENABLE_NVAPI)
    message(FATAL_ERROR "NVAPI not found! Either enable SLANG_RHI_FETCH_NVAPI or set SLANG_RHI_NVAPI_DIR to the NVAPI directory.")
endif()

# Fetch OptiX
if(SLANG_RHI_FETCH_OPTIX AND SLANG_RHI_ENABLE_CUDA)
    message(STATUS "Fetching OptiX headers ...")
    FetchPackage(optix URL ${SLANG_RHI_FETCH_OPTIX_URL})
    set(SLANG_RHI_OPTIX_INCLUDE_DIR ${optix_SOURCE_DIR})
endif()

# Setup OptiX
set(SLANG_RHI_OPTIX_INCLUDE_DIR ${SLANG_RHI_OPTIX_INCLUDE_DIR} CACHE STRING "OptiX include directory")
unset(SLANG_RHI_OPTIX_INCLUDE_DIR)
if(EXISTS "${SLANG_RHI_OPTIX_INCLUDE_DIR}/optix.h" AND SLANG_RHI_ENABLE_CUDA)
    add_library(slang-rhi-optix INTERFACE)
    target_include_directories(slang-rhi-optix INTERFACE ${SLANG_RHI_OPTIX_INCLUDE_DIR})
    target_link_libraries(slang-rhi PRIVATE slang-rhi-optix)
    set(SLANG_RHI_ENABLE_OPTIX ON)
else()
    set(SLANG_RHI_ENABLE_OPTIX OFF)
endif()

# Fetch and setup Google Dawn library (WebGPU implementation)
if(SLANG_RHI_FETCH_DAWN AND SLANG_RHI_ENABLE_WGPU)
    set(DAWN_VERSION ${SLANG_RHI_FETCH_DAWN_VERSION})

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(DAWN_OS "windows")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(DAWN_OS "linux")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(DAWN_OS "macos")
    endif()

    if(SLANG_RHI_ARCHITECTURE MATCHES "x86_64")
        set(DAWN_ARCH "x86_64")
    elseif(SLANG_RHI_ARCHITECTURE MATCHES "aarch64|arm64")
        set(DAWN_ARCH "aarch64")
    else()
        message(FATAL_ERROR "Unsupported processor architecture")
    endif()

    set(DAWN_URL "https://github.com/shader-slang/webgpu-dawn-binaries/releases/download/v${DAWN_VERSION}/webgpu-dawn-${DAWN_VERSION}-${DAWN_OS}-${DAWN_ARCH}.zip")
    message(STATUS "Fetching Dawn ${DAWN_VERSION} ...")
    FetchPackage(dawn URL ${DAWN_URL})
    set(SLANG_RHI_DAWN_DIR ${dawn_SOURCE_DIR})
endif()

# Setup Dawn
set(SLANG_RHI_DAWN_DIR ${SLANG_RHI_DAWN_DIR} CACHE STRING "Dawn directory")
unset(SLANG_RHI_DAWN_DIR)
if(EXISTS "${SLANG_RHI_DAWN_DIR}" AND SLANG_RHI_ENABLE_WGPU)
    set(SLANG_RHI_DAWN_INCLUDE_DIR ${SLANG_RHI_DAWN_DIR}/include CACHE STRING "Dawn include directory")
    set(SLANG_RHI_DAWN_LIB_DIR ${SLANG_RHI_DAWN_DIR}/lib CACHE STRING "Dawn lib directory")
    set(SLANG_RHI_DAWN_BIN_DIR ${SLANG_RHI_DAWN_DIR}/bin CACHE STRING "Dawn bin directory")
    # Use the variables from the cache.
    unset(SLANG_RHI_DAWN_INCLUDE_DIR)
    unset(SLANG_RHI_DAWN_LIB_DIR)
    unset(SLANG_RHI_DAWN_BIN_DIR)

    add_library(slang-rhi-dawn INTERFACE)
    target_include_directories(slang-rhi-dawn INTERFACE ${SLANG_RHI_DAWN_INCLUDE_DIR})

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        copy_file(${SLANG_RHI_DAWN_BIN_DIR}/dawn.dll .)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        copy_file(${SLANG_RHI_DAWN_LIB_DIR}64/libdawn.so .)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        copy_file(${SLANG_RHI_DAWN_LIB_DIR}/libdawn.dylib .)
    endif()

    target_link_libraries(slang-rhi PRIVATE slang-rhi-dawn)
elseif(SLANG_RHI_ENABLE_WGPU)
    message(FATAL_ERROR "Dawn not found! Either enable SLANG_RHI_FETCH_DAWN or set SLANG_RHI_DAWN_DIR to the Dawn directory.")
endif()

# Fetch glfw
if(SLANG_RHI_BUILD_TESTS OR SLANG_RHI_BUILD_EXAMPLES)
    FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG master)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF)
    FetchContent_MakeAvailable(glfw)
endif()

# Resources
cmrc_add_resource_library(
    slang-rhi-resources
    NAMESPACE resources
    src/cuda/kernels/clear-texture.cu
    src/metal/shaders/clear-texture.metal
)
set_target_properties(slang-rhi-resources PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(slang-rhi PRIVATE slang-rhi-resources)

# Generate config header
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/slang-rhi-config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/slang-rhi-config.h
)
target_include_directories(slang-rhi PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)

if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-assume -Wno-switch")
endif()

# nanothread
# TODO: disabled for now as it introduces a dependency to libatomic which leads to issues with cross compilation
# set(NANOTHREAD_STATIC ON)
# add_subdirectory(external/nanothread)
# set_target_properties(nanothread PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Setup compiler warnings
target_compile_options(slang-rhi PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 /WX
        /w14062 # enumerator 'identifier' in switch of enum 'enumeration' is not handled
        /wd4267 # conversion from 'size_t' to 'type', possible loss of data
        /wd4244 # conversion from 'type1' to 'type2', possible loss of data
        /wd4100 # unreferenced formal parameter
        /wd4018 # signed/unsigned mismatch
        /wd4245 # conversion from 'type1' to 'type2', signed/unsigned mismatch
        /wd4702 # unreachable code
        /wd4127 # conditional expression is constant
        /wd4389 # signed/unsigned mismatch
    >
    $<$<CXX_COMPILER_ID:GNU>:
        -Wall -Wextra -Wpedantic -Werror
        -Wshadow=local
        -Wno-unused-parameter
        -Wno-missing-field-initializers
        -Wno-sign-compare
    >
    $<$<CXX_COMPILER_ID:Clang>:
        -Wall -Wextra -Wpedantic -Werror
        -Wshadow
        -Wno-unknown-warning-option
        -Wno-unused-parameter
        -Wno-missing-field-initializers
        -Wno-sign-compare
        -Wno-gnu-anonymous-struct
        -Wno-gnu-zero-variadic-macro-arguments
        -Wno-nested-anon-types
        -Wno-cast-function-type-mismatch
        -Wno-c++20-extensions
        # Windows specific warnings
        -Wno-microsoft-exception-spec
        -Wno-microsoft-enum-value
        -Wno-microsoft-cast
        -Wno-microsoft-extra-qualification
        -Wno-language-extension-token
    >
    $<$<CXX_COMPILER_ID:AppleClang>:
        -Wall -Wextra -Wpedantic -Werror
        -Wshadow
        -Wno-unknown-warning-option
        -Wno-unused-parameter
        -Wno-missing-field-initializers
        -Wno-sign-compare
        -Wno-gnu-anonymous-struct
        -Wno-gnu-zero-variadic-macro-arguments
        -Wno-nested-anon-types
    >
)

target_sources(slang-rhi PRIVATE
    src/command-buffer.cpp
    src/command-list.cpp
    src/cuda-driver-api.cpp
    src/device.cpp
    src/enum-strings.cpp
    src/flag-combiner.cpp
    src/format-conversion.cpp
    src/pipeline.cpp
    src/resource-desc-utils.cpp
    src/rhi-shared.cpp
    src/rhi.cpp
    src/shader-object.cpp
    src/shader.cpp
    src/staging-heap.cpp
    src/core/assert.cpp
    src/core/blob.cpp
    src/core/offset-allocator.cpp
    src/core/platform.cpp
    src/core/sha1.cpp
    src/core/task-pool.cpp
    src/debug-layer/debug-command-buffer.cpp
    src/debug-layer/debug-command-encoder.cpp
    src/debug-layer/debug-command-queue.cpp
    src/debug-layer/debug-device.cpp
    src/debug-layer/debug-fence.cpp
    src/debug-layer/debug-helper-functions.cpp
    src/debug-layer/debug-query.cpp
    src/debug-layer/debug-shader-object.cpp
    src/debug-layer/debug-surface.cpp
)

if(APPLE)
    target_sources(slang-rhi PRIVATE
        src/cocoa-util.mm
    )
    target_link_libraries(slang-rhi INTERFACE "-framework Foundation" "-framework QuartzCore")
endif()

if(SLANG_RHI_ENABLE_CPU)
    target_sources(slang-rhi PRIVATE
        src/cpu/cpu-buffer.cpp
        src/cpu/cpu-command.cpp
        src/cpu/cpu-device.cpp
        src/cpu/cpu-fence.cpp
        src/cpu/cpu-helper-functions.cpp
        src/cpu/cpu-pipeline.cpp
        src/cpu/cpu-query.cpp
        src/cpu/cpu-shader-object-layout.cpp
        src/cpu/cpu-shader-object.cpp
        src/cpu/cpu-shader-program.cpp
        src/cpu/cpu-texture.cpp
    )
endif()

if(SLANG_RHI_ENABLE_D3D11 OR SLANG_RHI_HAS_D3D12)
    target_sources(slang-rhi PRIVATE
        src/d3d/d3d-util.cpp
        src/nvapi/nvapi-util.cpp
    )
endif()

if(SLANG_RHI_ENABLE_D3D11)
    target_sources(slang-rhi PRIVATE
        src/d3d11/d3d11-buffer.cpp
        src/d3d11/d3d11-command.cpp
        src/d3d11/d3d11-constant-buffer-pool.cpp
        src/d3d11/d3d11-device.cpp
        src/d3d11/d3d11-helper-functions.cpp
        src/d3d11/d3d11-input-layout.cpp
        src/d3d11/d3d11-pipeline.cpp
        src/d3d11/d3d11-query.cpp
        src/d3d11/d3d11-sampler.cpp
        src/d3d11/d3d11-shader-object-layout.cpp
        src/d3d11/d3d11-shader-object.cpp
        src/d3d11/d3d11-shader-program.cpp
        src/d3d11/d3d11-surface.cpp
        src/d3d11/d3d11-texture.cpp
    )
endif()

if(SLANG_RHI_ENABLE_D3D12)
    target_sources(slang-rhi PRIVATE
        src/d3d12/d3d12-acceleration-structure.cpp
        src/d3d12/d3d12-bindless-descriptor-set.cpp
        src/d3d12/d3d12-buffer.cpp
        src/d3d12/d3d12-command.cpp
        src/d3d12/d3d12-constant-buffer-pool.cpp
        src/d3d12/d3d12-descriptor-heap.cpp
        src/d3d12/d3d12-device.cpp
        src/d3d12/d3d12-fence.cpp
        src/d3d12/d3d12-helper-functions.cpp
        src/d3d12/d3d12-pipeline.cpp
        src/d3d12/d3d12-posix-synchapi.cpp
        src/d3d12/d3d12-query.cpp
        src/d3d12/d3d12-resource.cpp
        src/d3d12/d3d12-sampler.cpp
        src/d3d12/d3d12-shader-object-layout.cpp
        src/d3d12/d3d12-shader-object.cpp
        src/d3d12/d3d12-shader-program.cpp
        src/d3d12/d3d12-shader-table.cpp
        src/d3d12/d3d12-surface.cpp
        src/d3d12/d3d12-texture.cpp
    )
endif()

if(SLANG_RHI_ENABLE_VULKAN)
    target_sources(slang-rhi PRIVATE
        src/vulkan/vk-acceleration-structure.cpp
        src/vulkan/vk-api.cpp
        src/vulkan/vk-bindless-descriptor-set.cpp
        src/vulkan/vk-buffer.cpp
        src/vulkan/vk-command.cpp
        src/vulkan/vk-constant-buffer-pool.cpp
        src/vulkan/vk-descriptor-allocator.cpp
        src/vulkan/vk-device-queue.cpp
        src/vulkan/vk-device.cpp
        src/vulkan/vk-fence.cpp
        src/vulkan/vk-helper-functions.cpp
        src/vulkan/vk-module.cpp
        src/vulkan/vk-pipeline.cpp
        src/vulkan/vk-query.cpp
        src/vulkan/vk-sampler.cpp
        src/vulkan/vk-shader-object-layout.cpp
        src/vulkan/vk-shader-object.cpp
        src/vulkan/vk-shader-program.cpp
        src/vulkan/vk-shader-table.cpp
        src/vulkan/vk-surface.cpp
        src/vulkan/vk-texture.cpp
        src/vulkan/vk-util.cpp
    )
    add_library(slang-rhi-vulkan-headers INTERFACE)
    target_include_directories(slang-rhi-vulkan-headers INTERFACE external/vulkan-headers/include)
    target_link_libraries(slang-rhi PRIVATE slang-rhi-vulkan-headers)
endif()

if(SLANG_RHI_ENABLE_METAL)
    target_sources(slang-rhi PRIVATE
        src/metal/metal-acceleration-structure.cpp
        src/metal/metal-api.cpp
        src/metal/metal-buffer.cpp
        src/metal/metal-clear-engine.cpp
        src/metal/metal-command.cpp
        src/metal/metal-device.cpp
        src/metal/metal-fence.cpp
        src/metal/metal-helper-functions.cpp
        src/metal/metal-input-layout.cpp
        src/metal/metal-pipeline.cpp
        src/metal/metal-query.cpp
        src/metal/metal-sampler.cpp
        src/metal/metal-shader-object-layout.cpp
        src/metal/metal-shader-object.cpp
        src/metal/metal-shader-program.cpp
        src/metal/metal-shader-table.cpp
        src/metal/metal-surface.cpp
        src/metal/metal-texture.cpp
        src/metal/metal-util.cpp
    )
    add_library(slang-rhi-metal-cpp INTERFACE)
    target_include_directories(slang-rhi-metal-cpp INTERFACE external/metal-cpp)
    target_link_libraries(slang-rhi-metal-cpp INTERFACE "-framework Metal")
    target_link_libraries(slang-rhi PRIVATE slang-rhi-metal-cpp)
endif()

if(SLANG_RHI_ENABLE_CUDA)
    target_sources(slang-rhi PRIVATE
        src/cuda/cuda-acceleration-structure.cpp
        src/cuda/cuda-api.cpp
        src/cuda/cuda-buffer.cpp
        src/cuda/cuda-clear-engine.cpp
        src/cuda/cuda-command.cpp
        src/cuda/cuda-constant-buffer-pool.cpp
        src/cuda/cuda-device.cpp
        src/cuda/cuda-fence.cpp
        src/cuda/cuda-helper-functions.cpp
        src/cuda/cuda-nvrtc.cpp
        src/cuda/cuda-pipeline.cpp
        src/cuda/cuda-query.cpp
        src/cuda/cuda-shader-object-layout.cpp
        src/cuda/cuda-shader-object.cpp
        src/cuda/cuda-shader-program.cpp
        src/cuda/cuda-shader-table.cpp
        src/cuda/cuda-surface.cpp
        src/cuda/cuda-texture.cpp
    )
endif()

if(SLANG_RHI_ENABLE_WGPU)
    target_sources(slang-rhi PRIVATE
        src/wgpu/wgpu-api.cpp
        src/wgpu/wgpu-buffer.cpp
        src/wgpu/wgpu-command.cpp
        src/wgpu/wgpu-constant-buffer-pool.cpp
        src/wgpu/wgpu-device.cpp
        src/wgpu/wgpu-fence.cpp
        src/wgpu/wgpu-helper-functions.cpp
        src/wgpu/wgpu-input-layout.cpp
        src/wgpu/wgpu-pipeline.cpp
        src/wgpu/wgpu-query.cpp
        src/wgpu/wgpu-sampler.cpp
        src/wgpu/wgpu-shader-object-layout.cpp
        src/wgpu/wgpu-shader-object.cpp
        src/wgpu/wgpu-shader-program.cpp
        src/wgpu/wgpu-surface.cpp
        src/wgpu/wgpu-texture.cpp
        src/wgpu/wgpu-util.cpp
    )
endif()

target_include_directories(slang-rhi PUBLIC include)
target_include_directories(slang-rhi PRIVATE src)
# target_link_libraries(slang-rhi PRIVATE nanothread)
target_compile_definitions(slang-rhi
    PUBLIC
    SLANG_USER_CONFIG="slang-user-config.h"
    PRIVATE
    SLANG_RHI_DEBUG=$<BOOL:$<CONFIG:Debug>>
    $<$<CXX_COMPILER_ID:MSVC>:_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR>
    $<$<PLATFORM_ID:Windows>:NOMINMAX> # do not define min/max macros
    $<$<PLATFORM_ID:Windows>:UNICODE> # force character map to unicode
)
target_compile_features(slang-rhi PRIVATE cxx_std_17)
set_target_properties(slang-rhi PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(SLANG_RHI_BUILD_TESTS)
    add_library(doctest INTERFACE)
    target_include_directories(doctest INTERFACE external/doctest)

    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE external/stb)

    add_library(renderdoc INTERFACE)
    target_include_directories(renderdoc INTERFACE external/renderdoc)

    add_executable(slang-rhi-tests)
    target_sources(slang-rhi-tests PRIVATE
        tests/main.cpp
        tests/test-arena-allocator.cpp
        tests/test-benchmark-command.cpp
        tests/test-bindless-descriptor-handles.cpp
        tests/test-buffer-barrier.cpp
        tests/test-buffer-from-handle.cpp
        tests/test-buffer-shared.cpp
        tests/test-cmd-clear-buffer.cpp
        tests/test-cmd-clear-texture.cpp
        tests/test-cmd-copy-buffer.cpp
        tests/test-cmd-copy-buffer-to-texture.cpp
        tests/test-cmd-copy-texture.cpp
        tests/test-cmd-copy-texture-to-buffer.cpp
        tests/test-cmd-debug.cpp
        tests/test-cmd-draw.cpp
        tests/test-cmd-query.cpp
        tests/test-cmd-upload-buffer.cpp
        tests/test-cmd-upload-texture.cpp
        tests/test-compute-smoke.cpp
        tests/test-compute-trivial.cpp
        tests/test-cooperative-vector.cpp
        tests/test-device-from-handle.cpp
        tests/test-device-lifetime.cpp
        tests/test-fence.cpp
        tests/test-formats.cpp
        tests/test-link-time-constant.cpp
        tests/test-link-time-default.cpp
        tests/test-link-time-options.cpp
        tests/test-link-time-type.cpp
        tests/test-math.cpp
        tests/test-native-handle.cpp
        tests/test-nested-parameter-block.cpp
        tests/test-null-views.cpp
        tests/test-nvrtc.cpp
        tests/test-offset-allocator.cpp
        # tests/test-precompiled-module-cache.cpp
        tests/test-precompiled-module.cpp
        tests/test-ray-tracing.cpp
        tests/test-resolve-resource-tests.cpp
        tests/test-resource-states.cpp
        # tests/test-root-mutable-shader-object.cpp
        tests/test-root-shader-parameter.cpp
        tests/test-sampler-array.cpp
        tests/test-sampler.cpp
        tests/test-sha1.cpp
        tests/test-shader-cache.cpp
        tests/test-shader-object-resource-tracking.cpp
        tests/test-staging-heap.cpp
        tests/test-surface.cpp
        tests/test-texture-create.cpp
        tests/test-texture-layout.cpp
        tests/test-texture-shared.cpp
        tests/test-texture-view.cpp
        tests/test-texture-types.cpp
        tests/test-uint16-structured-buffer.cpp
        tests/testing.cpp
        tests/texture-utils.cpp
        tests/texture-test.cpp
        tests/test-task-pool.cpp
    )
    target_compile_definitions(slang-rhi-tests
        PRIVATE
        SLANG_RHI_DEBUG=$<BOOL:$<CONFIG:Debug>>
        SLANG_RHI_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests"
        SLANG_RHI_NVAPI_INCLUDE_DIR="${SLANG_RHI_NVAPI_INCLUDE_DIR}"
        SLANG_RHI_OPTIX_INCLUDE_DIR="${SLANG_RHI_OPTIX_INCLUDE_DIR}"
        $<$<PLATFORM_ID:Windows>:NOMINMAX> # do not define min/max macros
        $<$<PLATFORM_ID:Windows>:UNICODE> # force character map to unicode
    )
    target_compile_features(slang-rhi-tests PRIVATE cxx_std_17)
    target_include_directories(slang-rhi-tests PRIVATE tests src)
    target_link_libraries(slang-rhi-tests PRIVATE doctest stb slang slang-rhi glfw renderdoc)
    if(SLANG_RHI_ENABLE_OPTIX)
        target_link_libraries(slang-rhi-tests PRIVATE slang-rhi-optix)
    endif()

    add_test(NAME slang-rhi-tests COMMAND slang-rhi-tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests)
    set_property(TEST slang-rhi-tests PROPERTY ENVIRONMENT "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/$<CONFIG>/default.profraw")
endif()

if(SLANG_RHI_BUILD_EXAMPLES)
    function(add_example name source)
        add_executable(${name} ${source})
        target_compile_features(${name} PRIVATE cxx_std_17)
        target_include_directories(${name} PRIVATE examples/base)
        target_link_libraries(${name} PRIVATE slang slang-rhi glfw)
    endfunction()

    add_example(example-surface examples/surface/example-surface.cpp)
endif()

add_custom_target(slang-rhi-copy-files ALL DEPENDS ${SLANG_RHI_COPY_FILES})

# Add coverage target if coverage is enabled
if(SLANG_RHI_ENABLE_COVERAGE)
    # Find required tools without failing if not found
    if(APPLE)
        # On macOS, we need to use xcrun to access LLVM tools
        execute_process(
            COMMAND xcrun -f llvm-profdata
            OUTPUT_VARIABLE LLVM_PROFDATA
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND xcrun -f llvm-cov
            OUTPUT_VARIABLE LLVM_COV
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    else()
        find_program(LLVM_PROFDATA llvm-profdata QUIET)
        find_program(LLVM_COV llvm-cov QUIET)
    endif()

    # Only create coverage target if all required tools are found
    if(LLVM_PROFDATA AND LLVM_COV)
        message(STATUS "Found coverage tools: llvm-profdata, llvm-cov")

        set(LLVM_COV_ARGS
            $<TARGET_FILE:slang-rhi-tests>
            -instr-profile=coverage.profdata
            -ignore-filename-regex=".*build.*"
            -ignore-filename-regex=".*external.*"
            -ignore-filename-regex=".*tests.*"
        )

        # Create coverage target
        add_custom_target(coverage
            COMMAND ${LLVM_PROFDATA} merge -sparse ${CMAKE_BINARY_DIR}/$<CONFIG>/default.profraw -o ${CMAKE_BINARY_DIR}/coverage.profdata
            COMMAND ${LLVM_COV} export ${LLVM_COV_ARGS} -format lcov > coverage.lcov
            COMMAND ${LLVM_COV} report ${LLVM_COV_ARGS}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating coverage report..."
        )
    else()
        message(STATUS "Coverage tools not found. Coverage target will not be available.")
        message(STATUS "Required tools: llvm-profdata, llvm-cov")
    endif()
endif()

# Install
if(SLANG_RHI_INSTALL)
    install(
        TARGETS slang-rhi
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(
        DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
    )
    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/include/slang-rhi-config.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()
