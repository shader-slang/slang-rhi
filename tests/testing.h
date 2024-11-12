#pragma once

#include <doctest.h>
#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include "../src/core/blob.h"

#include <array>
#include <string_view>
#include <vector>
#include <cstring>

namespace rhi::testing {

/// Get name of running test suite (note: defined in main.cpp).
std::string getCurrentTestSuiteName();

/// Get name of running test case (note: defined in main.cpp).
std::string getCurrentTestCaseName();

/// Get global temp directory for tests.
std::string getTestTempDirectory();

/// Get temp directory for current test suite.
std::string getSuiteTempDirectory();

/// Get temp directory for current test case.
std::string getCaseTempDirectory();

/// Cleanup all temp directories created by tests.
void cleanupTestTempDirectories();

struct GpuTestContext
{
    slang::IGlobalSession* slangGlobalSession;
};

/// Helper function for print out diagnostic messages output by Slang compiler.
void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob);

/// Loads a compute shader module and produces a `IShaderProgram`.
Result loadComputeProgram(
    IDevice* device,
    ComPtr<IShaderProgram>& outShaderProgram,
    const char* shaderModuleName,
    const char* entryPointName,
    slang::ProgramLayout*& slangReflection
);

Result loadComputeProgram(
    IDevice* device,
    slang::ISession* slangSession,
    ComPtr<IShaderProgram>& outShaderProgram,
    const char* shaderModuleName,
    const char* entryPointName,
    slang::ProgramLayout*& slangReflection
);

Result loadComputeProgramFromSource(IDevice* device, ComPtr<IShaderProgram>& outShaderProgram, std::string_view source);

Result loadGraphicsProgram(
    IDevice* device,
    ComPtr<IShaderProgram>& outShaderProgram,
    const char* shaderModuleName,
    const char* vertexEntryPointName,
    const char* fragmentEntryPointName,
    slang::ProgramLayout*& slangReflection
);

/// Reads back the content of `buffer` and compares it against `expectedResult`.
void compareComputeResult(
    IDevice* device,
    IBuffer* buffer,
    size_t offset,
    const void* expectedResult,
    size_t expectedBufferSize
);

/// Reads back the content of `texture` and compares it against `expectedResult`.
void compareComputeResult(
    IDevice* device,
    ITexture* texture,
    void* expectedResult,
    size_t expectedResultRowPitch,
    size_t rowCount
);

template<typename T, size_t Count>
void compareResult(const T* result, const T* expectedResult)
{
    for (size_t i = 0; i < Count; i++)
    {
        CAPTURE(i);
        CHECK_EQ(result[i], expectedResult[i]);
    }
}

inline void compareResultFuzzy(const float* result, float* expectedResult, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        CAPTURE(i);
        CHECK_LE(result[i], expectedResult[i] + 0.01f);
        CHECK_GE(result[i], expectedResult[i] - 0.01f);
    }
}

template<typename T, size_t Count>
void compareComputeResult(IDevice* device, IBuffer* buffer, std::array<T, Count> expectedResult)
{
    size_t bufferSize = Count * sizeof(T);
    // Read back the results.
    ComPtr<ISlangBlob> bufferData;
    REQUIRE(!SLANG_FAILED(device->readBuffer(buffer, 0, bufferSize, bufferData.writeRef())));
    REQUIRE_EQ(bufferData->getBufferSize(), bufferSize);
    const T* result = reinterpret_cast<const T*>(bufferData->getBufferPointer());

    if constexpr (std::is_same<T, float>::value)
        compareResultFuzzy(result, expectedResult.data(), Count);
    else
        compareResult<T, Count>(result, expectedResult.data());
}

ComPtr<IDevice> createTestingDevice(
    GpuTestContext* ctx,
    DeviceType deviceType,
    bool useCachedDevice = true,
    std::vector<const char*> additionalSearchPaths = {}
);

void releaseCachedDevices();

ComPtr<slang::ISession> createTestingSession(
    GpuTestContext* ctx,
    DeviceType deviceType,
    std::vector<const char*> additionalSearchPaths = {}
);

bool isDeviceTypeAvailable(DeviceType deviceType);

bool isSwiftShaderDevice(IDevice* device);

std::vector<const char*> getSlangSearchPaths();

void initializeRenderDoc();
void renderDocBeginFrame();
void renderDocEndFrame();

template<typename T, typename... Args>
auto makeArray(Args... args)
{
    return std::array<T, sizeof...(Args)>{static_cast<T>(args)...};
}

using GpuTestFunc = void (*)(GpuTestContext*, DeviceType);

void runGpuTests(GpuTestFunc func, std::initializer_list<DeviceType> deviceTypes);

} // namespace rhi::testing

#define CHECK_CALL(x) CHECK(!SLANG_FAILED(x))
#define REQUIRE_CALL(x) REQUIRE(!SLANG_FAILED(x))

#define SKIP(msg)                                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        MESSAGE("Skipping (" msg ")");                                                                                 \
        return;                                                                                                        \
    }                                                                                                                  \
    while (0)
