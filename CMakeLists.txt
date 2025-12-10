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
option(SLANG_RHI_BUILD_TESTS_WITH_GLFW "Build tests that require GLFW" ${SLANG_RHI_MASTER_PROJECT})
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
if((SLANG_RHI_HAS_D3D11 OR SLANG_RHI_HAS_D3D12) AND (CMAKE_SYSTEM_NAME STREQUAL "Windows") AND (SLANG_RHI_ARCHITECTURE MATCHES "x86_64"))
    set(SLANG_RHI_HAS_NVAPI ON)
endif()

# Backend options
option(SLANG_RHI_ENABLE_CPU "Enable CPU backend" ON)
cmake_dependent_option(SLANG_RHI_ENABLE_D3D11 "Enable D3D11 backend" ON "SLANG_RHI_HAS_D3D11" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_D3D12 "Enable D3D12 backend" ON "SLANG_RHI_HAS_D3D12" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_AGILITY_SDK "Enable Agility SDK" ON "SLANG_RHI_HAS_AGILITY_SDK" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_NVAPI "Enable NVAPI support" ON "SLANG_RHI_HAS_NVAPI" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_VULKAN "Enable Vulkan backend" ON "SLANG_RHI_HAS_VULKAN" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_METAL "Enable Metal backend" ON "SLANG_RHI_HAS_METAL" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_CUDA "Enable CUDA backend" ON "SLANG_RHI_HAS_CUDA" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_OPTIX "Enable OptiX support" ON "SLANG_RHI_HAS_CUDA" OFF)
cmake_dependent_option(SLANG_RHI_ENABLE_WGPU "Enable WebGPU backend" ON "SLANG_RHI_HAS_WGPU" OFF)

# Enabled OptiX versions
if(NOT DEFINED SLANG_RHI_OPTIX_VERSIONS)
    set(SLANG_RHI_OPTIX_VERSIONS "8_0;8_1;9_0")
endif()

# Fetch slang options
option(SLANG_RHI_FETCH_SLANG "Fetch slang" ON)
set(SLANG_RHI_FETCH_SLANG_VERSION "2025.24" CACHE STRING "Slang version to fetch")

# slang-rhi depends on a few external libraries.
# These dependencies are fetched automatically if needed, using the URLs defined below.
# We don't use cache variables for the URLs to always fetch the required versions when switching branches or updating the repository.
# If you want to override the URLs, you can set the variables in the parent CMakeLists.txt before including this file
# or use cmake command line options like -DSLANG_RHI_DXC_URL=<url> populate cache variables to override the default URLs.

# DirectXShaderCompiler
if(NOT DEFINED SLANG_RHI_DXC_URL)
    set(SLANG_RHI_DXC_URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31.zip")
endif()

# WinPixEventRuntime
if(NOT DEFINED SLANG_RHI_WINPIX_URL)
    set(SLANG_RHI_WINPIX_URL "https://www.nuget.org/api/v2/package/WinPixEventRuntime/1.0.240308001")
endif()

# Agility SDK
if(NOT DEFINED SLANG_RHI_AGILITY_SDK_URL)
    set(SLANG_RHI_AGILITY_SDK_URL "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.616.1")
endif()
# If using a preview version of Agility SDK, set this to D3D12_PREVIEW_SDK_VERSION
if(NOT DEFINED SLANG_RHI_AGILITY_SDK_VERSION)
    set(SLANG_RHI_AGILITY_SDK_VERSION "D3D12_SDK_VERSION")
endif()

# NVAPI
if(NOT DEFINED SLANG_RHI_NVAPI_URL)
    set(SLANG_RHI_NVAPI_URL "https://github.com/NVIDIA/nvapi/archive/3d34a4faf095996663321646ebe003539a908f89.zip") # R580
endif()

# OptiX headers
if(NOT DEFINED SLANG_RHI_OPTIX_8_0_URL)
    set(SLANG_RHI_OPTIX_8_0_URL "https://github.com/NVIDIA/optix-dev/archive/f60c1e44f18426f426a2ed948f28515b3cf67b8a.zip")
endif()
if(NOT DEFINED SLANG_RHI_OPTIX_8_1_URL)
    set(SLANG_RHI_OPTIX_8_1_URL "https://github.com/NVIDIA/optix-dev/archive/50021ea0af6d41609a97777ceebbdf1e1d34efe7.zip")
endif()
if(NOT DEFINED SLANG_RHI_OPTIX_9_0_URL)
    set(SLANG_RHI_OPTIX_9_0_URL "https://github.com/NVIDIA/optix-dev/archive/a1280c1863ff19d87f8e827468a4cc906ba9032a.zip")
endif()

# Vulkan headers
if(NOT DEFINED SLANG_RHI_VULKAN_HEADERS_URL)
    set(SLANG_RHI_VULKAN_HEADERS_URL "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.318.zip")
endif()

# Metal C++ headers
if(NOT DEFINED SLANG_RHI_METAL_CPP_URL)
    set(SLANG_RHI_METAL_CPP_URL "https://developer.apple.com/metal/cpp/files/metal-cpp_macOS14.2_iOS17.2.zip")
endif()

# Dawn WebGPU
if(NOT DEFINED SLANG_RHI_DAWN_URL)
    set(DAWN_VERSION "135.0.7049.80")
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
    set(SLANG_RHI_DAWN_URL "https://github.com/shader-slang/webgpu-dawn-binaries/releases/download/v${DAWN_VERSION}/webgpu-dawn-${DAWN_VERSION}-${DAWN_OS}-${DAWN_ARCH}.zip")
endif()


if(SLANG_RHI_BUILD_SHARED)
    add_library(slang-rhi SHARED)
    set(SLANG_RHI_SHARED ON)
    target_compile_definitions(slang-rhi PRIVATE SLANG_RHI_SHARED_EXPORT)
else()
    add_library(slang-rhi STATIC)
    set(SLANG_RHI_SHARED OFF)
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
            set(SLANG_URL "${SLANG_URL}-linux-x86_64.tar.gz")
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
if(NOT SLANG_RHI_BUILD_FROM_SLANG_REPO)
    function(slang_rhi_add_library_slang)
        add_library(slang SHARED IMPORTED GLOBAL)

        # Slang renamed the compiler library from slang to slang-compiler in v2025.21
        # Detect if the old or new library name is used.
        if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND EXISTS ${SLANG_RHI_SLANG_BINARY_DIR}/bin/slang-compiler.dll)
            set(SLANG_LIB_NAME slang-compiler)
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS ${SLANG_RHI_SLANG_BINARY_DIR}/lib/libslang-compiler.so)
            set(SLANG_LIB_NAME slang-compiler)
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND EXISTS ${SLANG_RHI_SLANG_BINARY_DIR}/lib/libslang-compiler.dylib)
            set(SLANG_LIB_NAME slang-compiler)
        else()
            set(SLANG_LIB_NAME slang)
        endif()

        if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            set_target_properties(slang PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES ${SLANG_RHI_SLANG_INCLUDE_DIR}
                IMPORTED_IMPLIB ${SLANG_RHI_SLANG_BINARY_DIR}/lib/${SLANG_LIB_NAME}.lib
                IMPORTED_LOCATION ${SLANG_RHI_SLANG_BINARY_DIR}/bin/${SLANG_LIB_NAME}.dll
            )
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set_target_properties(slang PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES ${SLANG_RHI_SLANG_INCLUDE_DIR}
                IMPORTED_LOCATION ${SLANG_RHI_SLANG_BINARY_DIR}/lib/lib${SLANG_LIB_NAME}.so
            )
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set_target_properties(slang PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES ${SLANG_RHI_SLANG_INCLUDE_DIR}
                IMPORTED_LOCATION ${SLANG_RHI_SLANG_BINARY_DIR}/lib/lib${SLANG_LIB_NAME}.dylib
            )
        endif()
    endfunction()

    slang_rhi_add_library_slang()

    target_link_libraries(slang-rhi PUBLIC slang)

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        copy_file(${SLANG_RHI_SLANG_BINARY_DIR}/bin/slang.dll .)
        copy_file(${SLANG_RHI_SLANG_BINARY_DIR}/bin/slang-compiler.dll .)
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

# Fetch & setup DirectXShaderCompiler
if(SLANG_RHI_ENABLE_D3D12 AND (CMAKE_SYSTEM_NAME STREQUAL "Windows"))
    message(STATUS "Fetching DirectXShaderCompiler ...")
    FetchPackage(dxc URL ${SLANG_RHI_DXC_URL})
    if(SLANG_RHI_ARCHITECTURE MATCHES "x86_64")
        copy_file(${dxc_SOURCE_DIR}/bin/x64/dxcompiler.dll .)
        copy_file(${dxc_SOURCE_DIR}/bin/x64/dxil.dll .)
    elseif(SLANG_RHI_ARCHITECTURE MATCHES "aarch64|arm64")
        copy_file(${dxc_SOURCE_DIR}/bin/arm64/dxcompiler.dll .)
        copy_file(${dxc_SOURCE_DIR}/bin/arm64/dxil.dll .)
    endif()
endif()

# Fetch & setup WinPixEventRuntime
if(SLANG_RHI_ENABLE_D3D12 AND (CMAKE_SYSTEM_NAME STREQUAL "Windows"))
    message(STATUS "Fetching WinPixEventRuntime ...")
    FetchPackage(winpix URL ${SLANG_RHI_WINPIX_URL})
    if(SLANG_RHI_ARCHITECTURE MATCHES "x86_64")
        copy_file(${winpix_SOURCE_DIR}/bin/x64/WinPixEventRuntime.dll .)
    elseif(SLANG_RHI_ARCHITECTURE MATCHES "aarch64|arm64")
        copy_file(${winpix_SOURCE_DIR}/bin/arm64/WinPixEventRuntime.dll .)
    endif()
endif()

# Fetch & setup Agility SDK
if(SLANG_RHI_ENABLE_AGILITY_SDK AND (CMAKE_SYSTEM_NAME STREQUAL "Windows"))
    message(STATUS "Fetching Agility SDK ...")
    FetchPackage(agility_sdk URL ${SLANG_RHI_AGILITY_SDK_URL})
    if(SLANG_RHI_ARCHITECTURE MATCHES "x86_64")
        copy_file(${agility_sdk_SOURCE_DIR}/build/native/bin/x64/D3D12Core.dll D3D12)
        copy_file(${agility_sdk_SOURCE_DIR}/build/native/bin/x64/d3d12SDKLayers.dll D3D12)
    elseif(SLANG_RHI_ARCHITECTURE MATCHES "aarch64|arm64")
        copy_file(${agility_sdk_SOURCE_DIR}/build/native/bin/arm64/D3D12Core.dll D3D12)
        copy_file(${agility_sdk_SOURCE_DIR}/build/native/bin/arm64/d3d12SDKLayers.dll D3D12)
    endif()
    add_library(slang-rhi-agility-sdk INTERFACE)
    target_include_directories(slang-rhi-agility-sdk INTERFACE ${agility_sdk_SOURCE_DIR}/build/native/include)
    target_link_libraries(slang-rhi PUBLIC slang-rhi-agility-sdk)
endif()

# Fetch & setup NVAPI
if(SLANG_RHI_ENABLE_NVAPI)
    message(STATUS "Fetching NVAPI ...")
    FetchPackage(nvapi URL ${SLANG_RHI_NVAPI_URL})
    add_library(slang-rhi-nvapi INTERFACE)
    target_include_directories(slang-rhi-nvapi INTERFACE ${nvapi_SOURCE_DIR})
    target_link_libraries(slang-rhi-nvapi INTERFACE ${nvapi_SOURCE_DIR}/amd64/nvapi64.lib)
    target_link_libraries(slang-rhi PRIVATE slang-rhi-nvapi)
    set(SLANG_RHI_NVAPI_INCLUDE_DIR ${nvapi_SOURCE_DIR})
endif()

# Fetch & setup OptiX headers
if(SLANG_RHI_ENABLE_OPTIX)
    message(STATUS "Fetching OptiX headers ...")
    foreach(optix_version IN LISTS SLANG_RHI_OPTIX_VERSIONS)
        set(fetch_name "optix_${optix_version}")
        set(fetch_url "${SLANG_RHI_OPTIX_${optix_version}_URL}")
        FetchPackage(${fetch_name} URL ${fetch_url})
        set(target_name "slang-rhi-optix-${optix_version}")
        add_library(${target_name} INTERFACE)
        target_include_directories(${target_name} INTERFACE "${${fetch_name}_SOURCE_DIR}/include")
        # Copy device side headers
        set(src_dir "${${fetch_name}_SOURCE_DIR}/include")
        set(dst_dir "optix/${optix_version}")
        copy_file(${src_dir}/optix.h ${dst_dir})
        copy_file(${src_dir}/optix_device.h ${dst_dir})
        copy_file(${src_dir}/optix_types.h ${dst_dir})
        copy_file(${src_dir}/internal/optix_device_impl.h ${dst_dir}/internal)
        copy_file(${src_dir}/internal/optix_device_impl_transformations.h ${dst_dir}/internal)
    endforeach()
    set(SLANG_RHI_OPTIX_DEVICE_HEADER_INCLUDE_DIR ${SLANG_RHI_BINARY_DIR}/optix)
endif()

# Fetch & setup Vulkan headers
if(SLANG_RHI_ENABLE_VULKAN)
    message(STATUS "Fetching Vulkan headers ...")
    FetchPackage(vulkan_headers URL ${SLANG_RHI_VULKAN_HEADERS_URL})
    add_library(slang-rhi-vulkan-headers INTERFACE)
    target_include_directories(slang-rhi-vulkan-headers INTERFACE ${vulkan_headers_SOURCE_DIR}/include)
endif()

# Fetch & setup Metal C++ headers
if(SLANG_RHI_ENABLE_METAL)
    message(STATUS "Fetching Metal C++ headers ...")
    FetchPackage(metal_cpp URL ${SLANG_RHI_METAL_CPP_URL})
    add_library(slang-rhi-metal-cpp INTERFACE)
    target_include_directories(slang-rhi-metal-cpp INTERFACE ${metal_cpp_SOURCE_DIR})
endif()

# Fetch & setup Dawn WebGPU
if(SLANG_RHI_ENABLE_WGPU)
    message(STATUS "Fetching Dawn WebGPU ...")
    FetchPackage(dawn URL ${SLANG_RHI_DAWN_URL})
    add_library(slang-rhi-dawn INTERFACE)
    target_include_directories(slang-rhi-dawn INTERFACE ${dawn_SOURCE_DIR}/include)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        copy_file(${dawn_SOURCE_DIR}/bin/dawn.dll .)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        copy_file(${dawn_SOURCE_DIR}/lib64/libdawn.so .)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        copy_file(${dawn_SOURCE_DIR}/lib/libdawn.dylib .)
    endif()
    target_link_libraries(slang-rhi PRIVATE slang-rhi-dawn)
endif()

# Fetch glfw
if((SLANG_RHI_BUILD_TESTS AND SLANG_RHI_BUILD_TESTS_WITH_GLFW) OR SLANG_RHI_BUILD_EXAMPLES)
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

# Create an "x" for the enabled OptiX versions for use in the config header
# Sort the versions in descending order so that the highest version is first
set(SLANG_RHI_OPTIX_VERSIONS_SORTED ${SLANG_RHI_OPTIX_VERSIONS})
list(SORT SLANG_RHI_OPTIX_VERSIONS_SORTED COMPARE NATURAL)
list(REVERSE SLANG_RHI_OPTIX_VERSIONS_SORTED)
set(SLANG_RHI_OPTIX_VERSIONS_X "")
foreach(ver IN LISTS SLANG_RHI_OPTIX_VERSIONS_SORTED)
    string(APPEND SLANG_RHI_OPTIX_VERSIONS_X " x(v${ver})")
endforeach()

# Generate config header
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/slang-rhi-config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/slang-rhi-config.h
)
target_include_directories(slang-rhi PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)

if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-assume -Wno-switch")
endif()

# Setup compiler warnings
add_library(slang-rhi-warnings INTERFACE)
target_compile_options(slang-rhi-warnings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4
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
        -Wall -Wextra -Wpedantic
        -Wno-unknown-warning
        -Wshadow=compatible-local
        -Wno-unused-parameter
        -Wno-missing-field-initializers
        -Wno-sign-compare
        -Wno-subobject-linkage
    >
    $<$<CXX_COMPILER_ID:Clang>:
        -Wall -Wextra -Wpedantic
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
        -Wall -Wextra -Wpedantic
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
add_library(slang-rhi-warnings-as-errors INTERFACE)
target_compile_options(slang-rhi-warnings-as-errors INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/WX>
    $<$<CXX_COMPILER_ID:GNU>:-Werror>
    $<$<CXX_COMPILER_ID:Clang>:-Werror>
    $<$<CXX_COMPILER_ID:AppleClang>:-Werror>
)
target_link_libraries(slang-rhi PRIVATE slang-rhi-warnings slang-rhi-warnings-as-errors)

target_sources(slang-rhi PRIVATE
    src/command-buffer.cpp
    src/command-list.cpp
    src/cuda-driver-api.cpp
    src/device.cpp
    src/device-child.cpp
    src/enum-strings.cpp
    src/format-conversion.cpp
    src/heap.cpp
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
    src/core/smart-pointer.cpp
    src/core/task-pool.cpp
    src/core/timer.cpp
    src/debug-layer/debug-command-buffer.cpp
    src/debug-layer/debug-command-encoder.cpp
    src/debug-layer/debug-command-queue.cpp
    src/debug-layer/debug-device.cpp
    src/debug-layer/debug-fence.cpp
    src/debug-layer/debug-heap.cpp
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
        src/d3d/d3d-utils.cpp
        src/nvapi/nvapi-util.cpp
    )
endif()

if(SLANG_RHI_ENABLE_D3D11)
    target_sources(slang-rhi PRIVATE
        src/d3d11/d3d11-buffer.cpp
        src/d3d11/d3d11-command.cpp
        src/d3d11/d3d11-constant-buffer-pool.cpp
        src/d3d11/d3d11-device.cpp
        src/d3d11/d3d11-input-layout.cpp
        src/d3d11/d3d11-pipeline.cpp
        src/d3d11/d3d11-query.cpp
        src/d3d11/d3d11-sampler.cpp
        src/d3d11/d3d11-shader-object-layout.cpp
        src/d3d11/d3d11-shader-object.cpp
        src/d3d11/d3d11-shader-program.cpp
        src/d3d11/d3d11-surface.cpp
        src/d3d11/d3d11-texture.cpp
        src/d3d11/d3d11-utils.cpp
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
        src/d3d12/d3d12-utils.cpp
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
        src/vulkan/vk-heap.cpp
        src/vulkan/vk-pipeline.cpp
        src/vulkan/vk-query.cpp
        src/vulkan/vk-sampler.cpp
        src/vulkan/vk-shader-object-layout.cpp
        src/vulkan/vk-shader-object.cpp
        src/vulkan/vk-shader-program.cpp
        src/vulkan/vk-shader-table.cpp
        src/vulkan/vk-surface.cpp
        src/vulkan/vk-texture.cpp
        src/vulkan/vk-utils.cpp
    )
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
        src/metal/metal-utils.cpp
    )
    target_link_libraries(slang-rhi-metal-cpp INTERFACE "-framework Metal")
    target_link_libraries(slang-rhi PRIVATE slang-rhi-metal-cpp)
endif()

if(SLANG_RHI_ENABLE_CUDA)
    target_sources(slang-rhi PRIVATE
        src/cuda/cuda-acceleration-structure.cpp
        src/cuda/cuda-buffer.cpp
        src/cuda/cuda-clear-engine.cpp
        src/cuda/cuda-command.cpp
        src/cuda/cuda-constant-buffer-pool.cpp
        src/cuda/cuda-device.cpp
        src/cuda/cuda-fence.cpp
        src/cuda/cuda-heap.cpp
        src/cuda/cuda-nvrtc.cpp
        src/cuda/cuda-pipeline.cpp
        src/cuda/cuda-query.cpp
        src/cuda/cuda-sampler.cpp
        src/cuda/cuda-shader-object-layout.cpp
        src/cuda/cuda-shader-object.cpp
        src/cuda/cuda-shader-program.cpp
        src/cuda/cuda-shader-table.cpp
        src/cuda/cuda-surface.cpp
        src/cuda/cuda-texture.cpp
        src/cuda/cuda-utils.cpp
        src/cuda/optix-api.cpp
    )
    if(SLANG_RHI_ENABLE_OPTIX)
        foreach(optix_version IN LISTS SLANG_RHI_OPTIX_VERSIONS)
            target_sources(slang-rhi PRIVATE "src/cuda/optix-api-impl-${optix_version}.cpp")
            get_target_property(optix_include_dir "slang-rhi-optix-${optix_version}" INTERFACE_INCLUDE_DIRECTORIES)
            set_source_files_properties(src/cuda/optix-api-impl-${optix_version}.cpp PROPERTIES COMPILE_FLAGS "-I${optix_include_dir}")
        endforeach()
    endif()
endif()

if(SLANG_RHI_ENABLE_WGPU)
    target_sources(slang-rhi PRIVATE
        src/wgpu/wgpu-api.cpp
        src/wgpu/wgpu-buffer.cpp
        src/wgpu/wgpu-command.cpp
        src/wgpu/wgpu-constant-buffer-pool.cpp
        src/wgpu/wgpu-device.cpp
        src/wgpu/wgpu-fence.cpp
        src/wgpu/wgpu-input-layout.cpp
        src/wgpu/wgpu-pipeline.cpp
        src/wgpu/wgpu-query.cpp
        src/wgpu/wgpu-sampler.cpp
        src/wgpu/wgpu-shader-object-layout.cpp
        src/wgpu/wgpu-shader-object.cpp
        src/wgpu/wgpu-shader-program.cpp
        src/wgpu/wgpu-surface.cpp
        src/wgpu/wgpu-texture.cpp
        src/wgpu/wgpu-utils.cpp
    )
endif()

target_include_directories(slang-rhi PUBLIC include)
target_include_directories(slang-rhi PRIVATE src)
target_compile_definitions(slang-rhi
    PUBLIC
    SLANG_USER_CONFIG="slang-user-config.h"
    PRIVATE
    SLANG_RHI_DEBUG=$<BOOL:$<CONFIG:Debug>>
    $<$<CXX_COMPILER_ID:MSVC>:_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR>
    $<$<PLATFORM_ID:Windows>:NOMINMAX> # do not define min/max macros
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN> # reduce windows.h footprint
    $<$<PLATFORM_ID:Windows>:UNICODE> # force character map to unicode
)
target_compile_features(slang-rhi PRIVATE cxx_std_17)
set_target_properties(slang-rhi PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Tests currently require linking statically due to reaching into internal details of the RHI implementation.
if(SLANG_RHI_BUILD_TESTS AND NOT SLANG_RHI_BUILD_SHARED)
    add_library(doctest INTERFACE)
    target_include_directories(doctest INTERFACE external/doctest)

    add_library(slang-rhi-stb INTERFACE)
    target_include_directories(slang-rhi-stb INTERFACE external/stb)

    add_library(renderdoc INTERFACE)
    target_include_directories(renderdoc INTERFACE external/renderdoc)

    add_executable(slang-rhi-tests)
    target_sources(slang-rhi-tests PRIVATE
        tests/main.cpp
        tests/test-arena-allocator.cpp
        tests/test-benchmark-command.cpp
        tests/test-bindless.cpp
        tests/test-bind-pointers.cpp
        tests/test-blob.cpp
        tests/test-buffer-barrier.cpp
        tests/test-buffer-from-handle.cpp
        tests/test-buffer-shared.cpp
        tests/test-command-encoder.cpp
        tests/test-compilation-report.cpp
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
        tests/test-combined-texture-sampler.cpp
        tests/test-compute-smoke.cpp
        tests/test-compute-trivial.cpp
        tests/test-cooperative-vector.cpp
        tests/test-cuda-external-devices.cpp
        tests/test-device-from-handle.cpp
        tests/test-device-lifetime.cpp
        tests/test-fence.cpp
        tests/test-formats.cpp
        tests/test-heap.cpp
        tests/test-link-time-constant.cpp
        tests/test-link-time-default.cpp
        tests/test-link-time-options.cpp
        tests/test-link-time-type.cpp
        tests/test-math.cpp
        tests/test-native-handle.cpp
        tests/test-nested-parameter-block.cpp
        tests/test-null-views.cpp
        tests/test-nvrtc.cpp
        tests/test-nvapi.cpp
        tests/test-offset-allocator.cpp
        tests/test-pipeline-cache.cpp
        # tests/test-precompiled-module-cache.cpp
        tests/test-precompiled-module.cpp
        tests/test-ray-tracing-clusters.cpp
        tests/test-ray-tracing-hitobject-intrinsics.cpp
        tests/test-ray-tracing-intrinsics.cpp
        tests/test-ray-tracing-transform-query.cpp
        tests/test-ray-tracing-lss.cpp
        tests/test-ray-tracing-raygen-entrypoint.cpp
        tests/test-ray-tracing-reorder.cpp
        tests/test-ray-tracing-sphere.cpp
        tests/test-ray-tracing.cpp
        tests/test-resolve-resource-tests.cpp
        tests/test-resource-states.cpp
        # tests/test-root-mutable-shader-object.cpp
        tests/test-root-shader-parameter.cpp
        tests/test-sampler-array.cpp
        tests/test-sampler.cpp
        tests/test-sha1.cpp
        tests/test-shader-cache.cpp
        tests/test-shader-object-large.cpp
        tests/test-shader-object-resource-tracking.cpp
        tests/test-staging-heap.cpp
        tests/test-surface.cpp
        tests/test-texture-create.cpp
        tests/test-texture-layout.cpp
        tests/test-texture-shared.cpp
        tests/test-texture-types.cpp
        tests/test-texture-view-3d.cpp
        tests/test-texture-view.cpp
        tests/test-timer.cpp
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
        SLANG_RHI_OPTIX_DEVICE_HEADER_INCLUDE_DIR="${SLANG_RHI_OPTIX_DEVICE_HEADER_INCLUDE_DIR}"
        SLANG_RHI_BUILD_TESTS_WITH_GLFW=$<BOOL:${SLANG_RHI_BUILD_TESTS_WITH_GLFW}>
        $<$<PLATFORM_ID:Windows>:NOMINMAX> # do not define min/max macros
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN> # reduce windows.h footprint
        $<$<PLATFORM_ID:Windows>:UNICODE> # force character map to unicode
    )
    target_compile_features(slang-rhi-tests PRIVATE cxx_std_17)
    target_include_directories(slang-rhi-tests PRIVATE tests src)
    target_link_libraries(slang-rhi-tests PRIVATE slang-rhi-warnings slang-rhi-warnings-as-errors)
    target_link_libraries(slang-rhi-tests PRIVATE doctest slang-rhi-stb slang slang-rhi $<$<BOOL:${SLANG_RHI_BUILD_TESTS_WITH_GLFW}>:glfw> renderdoc)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(Threads REQUIRED)
        target_link_libraries(slang-rhi-tests PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/$<CONFIG>/nsystests.bat INPUT ${CMAKE_CURRENT_SOURCE_DIR}/tools/nsystests.bat.ln)
    endif()

endif()

if(SLANG_RHI_BUILD_EXAMPLES)
    function(add_example name source)
        add_executable(${name} ${source})
        target_compile_features(${name} PRIVATE cxx_std_17)
        get_filename_component(EXAMPLE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${source}" PATH)
        target_compile_definitions(${name} PRIVATE SLANG_RHI_DEBUG=$<BOOL:$<CONFIG:Debug>> EXAMPLE_DIR="${EXAMPLE_DIR}" NOMINMAX)
        target_include_directories(${name} PRIVATE examples/base)
        target_link_libraries(${name} PRIVATE slang-rhi slang glfw)
    endfunction()

    add_example(example-shader-toy examples/shader-toy/example-shader-toy.cpp)
    add_example(example-surface examples/surface/example-surface.cpp)
    add_example(example-triangle examples/triangle/example-triangle.cpp)
endif()

add_custom_target(slang-rhi-copy-files ALL DEPENDS ${SLANG_RHI_COPY_FILES})
add_dependencies(slang-rhi slang-rhi-copy-files)

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
