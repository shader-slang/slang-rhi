// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <doctest.h>
#include <slang-rhi.h>

#include "shader-cursor.h"

#include <string_view>
#include <array>
#include <vector>

namespace gfx::testing {

struct GpuTestContext {
    slang::IGlobalSession* slangGlobalSession;
    // IDevice* device;
};

void run_gpu_test(void (*func)(GpuTestContext*));

void release_cached_devices();

/// Helper function for print out diagnostic messages output by Slang compiler.
void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob);

/// Loads a compute shader module and produces a `gfx::IShaderProgram`.
Slang::Result loadComputeProgram(
    gfx::IDevice* device,
    Slang::ComPtr<gfx::IShaderProgram>& outShaderProgram,
    const char* shaderModuleName,
    const char* entryPointName,
    slang::ProgramLayout*& slangReflection);

Slang::Result loadComputeProgram(
    gfx::IDevice* device,
    slang::ISession* slangSession,
    Slang::ComPtr<gfx::IShaderProgram>& outShaderProgram,
    const char* shaderModuleName,
    const char* entryPointName,
    slang::ProgramLayout*& slangReflection);

Slang::Result loadComputeProgramFromSource(
    gfx::IDevice* device,
    Slang::ComPtr<gfx::IShaderProgram>& outShaderProgram,
    std::string_view source);

Slang::Result loadGraphicsProgram(
    gfx::IDevice* device,
    Slang::ComPtr<gfx::IShaderProgram>& outShaderProgram,
    const char* shaderModuleName,
    const char* vertexEntryPointName,
    const char* fragmentEntryPointName,
    slang::ProgramLayout*& slangReflection);

    /// Reads back the content of `buffer` and compares it against `expectedResult`.
void compareComputeResult(
    gfx::IDevice* device,
    gfx::IBufferResource* buffer,
    size_t offset,
    const void* expectedResult,
    size_t expectedBufferSize);

/// Reads back the content of `texture` and compares it against `expectedResult`.
void compareComputeResult(
    gfx::IDevice* device,
    gfx::ITextureResource* texture,
    gfx::ResourceState state,
    void* expectedResult,
    size_t expectedResultRowPitch,
    size_t rowCount);

void compareComputeResultFuzzy(
    const float* result,
    float* expectedResult,
    size_t expectedBufferSize);

    /// Reads back the content of `buffer` and compares it against `expectedResult` with a set tolerance.
void compareComputeResultFuzzy(
    gfx::IDevice* device,
    gfx::IBufferResource* buffer,
    float* expectedResult,
    size_t expectedBufferSize);

template<typename T, size_t Count>
void compareComputeResult(
    gfx::IDevice* device,
    gfx::IBufferResource* buffer,
    std::array<T, Count> expectedResult)
{
    if constexpr (std::is_same<T, float>::value)
        return compareComputeResultFuzzy(device, buffer, expectedResult.data(), expectedResult.size());
    else
        return compareComputeResult(device, buffer, 0, expectedResult.data(), expectedResult.size());
}

Slang::ComPtr<gfx::IDevice> createTestingDevice(
    GpuTestContext* ctx,
    DeviceType deviceType,
    bool useCachedDevice = true,
    std::vector<const char*> additionalSearchPaths = {});

bool isSwiftShaderDevice(IDevice* device);

std::vector<const char*> getSlangSearchPaths();

void initializeRenderDoc();
void renderDocBeginFrame();
void renderDocEndFrame();

using GpuTestFunc = void (*)(GpuTestContext*, DeviceType);

void runGpuTests(GpuTestFunc func, std::initializer_list<DeviceType> deviceTypes);

} // namespace gfx::testing

#define GFX_CHECK_CALL(x) CHECK(!SLANG_FAILED(x))
#define GFX_CHECK_CALL_ABORT(x) REQUIRE(!SLANG_FAILED(x))
