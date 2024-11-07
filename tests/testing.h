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
    ComPtr<IDevice> device;
    DeviceType deviceType;
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
    GpuTestContext& ctx,
    DeviceType deviceType,
    bool useCachedDevice = true,
    std::vector<const char*> additionalSearchPaths = {}
);

void releaseCachedDevices();

ComPtr<slang::ISession> createTestingSession(
    GpuTestContext& ctx,
    DeviceType deviceType,
    std::vector<const char*> additionalSearchPaths = {}
);

bool isSwiftShaderDevice(IDevice* device);

slang::IGlobalSession* getSlangGlobalSession();

std::vector<const char*> getSlangSearchPaths();

void initializeRenderDoc();
void renderDocBeginFrame();
void renderDocEndFrame();

template<typename T, typename... Args>
auto makeArray(Args... args)
{
    return std::array<T, sizeof...(Args)>{static_cast<T>(args)...};
}

using GpuTestFunc = void (*)(GpuTestContext&);

inline const char* deviceTypeToString(DeviceType deviceType)
{
    switch (deviceType)
    {
    case DeviceType::D3D11:
        return "d3d11";
    case DeviceType::D3D12:
        return "d3d12";
    case DeviceType::Vulkan:
        return "vulkan";
    case DeviceType::Metal:
        return "metal";
    case DeviceType::CPU:
        return "cpu";
    case DeviceType::CUDA:
        return "cuda";
    case DeviceType::WGPU:
        return "wgpu";
    default:
        return "unknown";
    }
}

enum DeviceTypeFlag
{
    D3D11 = (1 << (int)DeviceType::D3D11),
    D3D12 = (1 << (int)DeviceType::D3D12),
    Vulkan = (1 << (int)DeviceType::Vulkan),
    Metal = (1 << (int)DeviceType::Metal),
    CPU = (1 << (int)DeviceType::CPU),
    CUDA = (1 << (int)DeviceType::CUDA),
    WGPU = (1 << (int)DeviceType::WGPU),
    ALL = D3D11 | D3D12 | Vulkan | Metal | CPU | CUDA | WGPU,
};

} // namespace rhi::testing

#define GPU_TEST_CASE_IMPL(name, func, deviceTypes)                                                                    \
    static void func(::rhi::testing::GpuTestContext& ctx);                                                             \
    TEST_CASE(name)                                                                                                    \
    {                                                                                                                  \
        for (int i = 0; i < 7; ++i)                                                                                    \
        {                                                                                                              \
            ::rhi::DeviceType deviceType = ::rhi::DeviceType(i);                                                       \
            if (((deviceTypes) & (1 << i)) == 0)                                                                       \
            {                                                                                                          \
                continue;                                                                                              \
            }                                                                                                          \
            if (!getRHI()->isDeviceTypeSupported(deviceType))                                                          \
            {                                                                                                          \
                continue;                                                                                              \
            }                                                                                                          \
            SUBCASE(deviceTypeToString(deviceType))                                                                    \
            {                                                                                                          \
                ::rhi::testing::GpuTestContext ctx;                                                                    \
                ctx.slangGlobalSession = ::rhi::testing::getSlangGlobalSession();                                      \
                ctx.device = ::rhi::testing::createTestingDevice(ctx, deviceType);                                     \
                ctx.deviceType = deviceType;                                                                           \
                func(ctx);                                                                                             \
            }                                                                                                          \
        }                                                                                                              \
    }                                                                                                                  \
    static void func(::rhi::testing::GpuTestContext& ctx)

#define GPU_TEST_CASE(name, deviceTypes) GPU_TEST_CASE_IMPL(name, DOCTEST_ANONYMOUS(GPU_TEST_ANONYMOUS_), deviceTypes)

#define CHECK_CALL(x) CHECK(!SLANG_FAILED(x))
#define REQUIRE_CALL(x) REQUIRE(!SLANG_FAILED(x))

#define SKIP(msg)                                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        MESSAGE("Skipping (" msg ")");                                                                                 \
        return;                                                                                                        \
    }                                                                                                                  \
    while (0)
