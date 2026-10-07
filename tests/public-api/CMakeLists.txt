cmake_minimum_required(VERSION 3.20)
project(slang-rhi-installed-test LANGUAGES CXX)

# Configure this project separately, without add_subdirectory(slang-rhi) or any
# build-tree RHI include/library paths. Slang's SDK headers are a prerequisite.
set(RHI_INSTALL_DIR "" CACHE PATH "Installed slang-rhi prefix")
set(RHI_INSTALL_LIBDIR "lib" CACHE STRING "Installed library directory")
set(SLANG_INCLUDE_DIR "" CACHE PATH "Slang SDK include directory")
set(SLANG_LIBRARY_DIR "" CACHE PATH "Slang SDK library directory")
find_path(RHI_INCLUDE_DIR slang-rhi.h PATHS "${RHI_INSTALL_DIR}/include" NO_DEFAULT_PATH REQUIRED)
find_library(RHI_LIBRARY NAMES slang-rhi
    PATHS "${RHI_INSTALL_DIR}/${RHI_INSTALL_LIBDIR}" NO_DEFAULT_PATH REQUIRED)
if(NOT EXISTS "${SLANG_INCLUDE_DIR}/slang.h")
    message(FATAL_ERROR "SLANG_INCLUDE_DIR must point to the Slang SDK headers")
endif()
# ShaderCursor's inline reflection helpers call the Slang C API directly.
# Unix installs bundle Slang's runtime libraries; Windows uses its SDK import library.
if(WIN32)
    find_library(SLANG_LIBRARY NAMES slang-compiler slang
        PATHS "${SLANG_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
else()
    # Do not fall back to the SDK here: that could conceal missing runtime files
    # in the installation or add the original SDK location to the test's RPATH.
    find_library(SLANG_LIBRARY NAMES slang-compiler slang
        PATHS "${RHI_INSTALL_DIR}/${RHI_INSTALL_LIBDIR}" NO_DEFAULT_PATH REQUIRED)
endif()

add_executable(slang-rhi-installed-test main.cpp)
target_compile_features(slang-rhi-installed-test PRIVATE cxx_std_17)
target_include_directories(slang-rhi-installed-test PRIVATE "${RHI_INCLUDE_DIR}" "${SLANG_INCLUDE_DIR}")
target_link_libraries(slang-rhi-installed-test PRIVATE "${RHI_LIBRARY}" "${SLANG_LIBRARY}")
if(MSVC)
    target_compile_options(slang-rhi-installed-test PRIVATE /EHsc)
endif()
