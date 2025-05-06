#pragma once

#include <doctest.h>
#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include "enum-strings.h"
#include "../src/core/blob.h"
#include "../src/core/span.h"

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

std::string readFile(std::string_view path);
void writeFile(std::string_view path, const void* data, size_t size);
inline void writeFile(std::string_view path, std::string_view data)
{
    writeFile(path, data.data(), data.size());
}

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

template<typename T>
void compareResult(const T* result, const T* expectedResult, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        CAPTURE(i);
        CHECK_EQ(result[i], expectedResult[i]);
    }
}

inline void compareResultFuzzy(const float* result, const float* expectedResult, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        CAPTURE(i);
        CHECK_LE(result[i], expectedResult[i] + 0.01f);
        CHECK_GE(result[i], expectedResult[i] - 0.01f);
    }
}

template<typename T>
void compareComputeResult(IDevice* device, IBuffer* buffer, span<T> expectedResult)
{
    size_t bufferSize = expectedResult.size() * sizeof(T);
    // Read back the results.
    ComPtr<ISlangBlob> bufferData;
    REQUIRE(SLANG_SUCCEEDED(device->readBuffer(buffer, 0, bufferSize, bufferData.writeRef())));
    REQUIRE(bufferData->getBufferSize() == bufferSize);
    const T* result = reinterpret_cast<const T*>(bufferData->getBufferPointer());

    if constexpr (std::is_same<T, float>::value)
        compareResultFuzzy(result, expectedResult.data(), expectedResult.size());
    else
        compareResult<T>(result, expectedResult.data(), expectedResult.size());
}

template<typename T, size_t Count>
void compareComputeResult(IDevice* device, IBuffer* buffer, std::array<T, Count> expectedResult)
{
    compareComputeResult(device, buffer, span<T>(expectedResult.data(), Count));
}

template<typename T>
void compareComputeResult(IDevice* device, ITexture* texture, uint32_t layer, uint32_t mip, span<T> expectedResult)
{
    size_t bufferSize = expectedResult.size() * sizeof(T);
    // Read back the results.
    ComPtr<ISlangBlob> textureData;
    SubresourceLayout layout;
    REQUIRE(SLANG_SUCCEEDED(device->readTexture(texture, layer, mip, textureData.writeRef(), &layout)));
    REQUIRE(textureData->getBufferSize() >= bufferSize);

    uint8_t* buffer = (uint8_t*)textureData->getBufferPointer();
    for (uint32_t z = 0; z < layout.size.depth; z++)
    {
        for (uint32_t y = 0; y < layout.size.height; y++)
        {
            for (uint32_t x = 0; x < layout.size.width; x++)
            {
                const uint8_t* src = reinterpret_cast<const uint8_t*>(
                    buffer + z * layout.slicePitch + y * layout.rowPitch + x * layout.colPitch
                );
                uint8_t* dst = reinterpret_cast<uint8_t*>(
                    buffer + (((z * layout.size.depth + y) * layout.size.width) + x) * layout.colPitch
                );
                ::memcpy(dst, src, layout.colPitch);
            }
        }
    }

    const T* result = reinterpret_cast<const T*>(textureData->getBufferPointer());

    if constexpr (std::is_same<T, float>::value)
        compareResultFuzzy(result, expectedResult.data(), expectedResult.size());
    else
        compareResult<T>(result, expectedResult.data(), expectedResult.size());
}

template<typename T, size_t Count>
void compareComputeResult(
    IDevice* device,
    ITexture* texture,
    uint32_t layer,
    uint32_t mip,
    std::array<T, Count> expectedResult
)
{
    compareComputeResult(device, texture, layer, mip, span<T>(expectedResult.data(), Count));
}

ComPtr<IDevice> createTestingDevice(
    GpuTestContext* ctx,
    DeviceType deviceType,
    bool useCachedDevice = true,
    std::vector<const char*> additionalSearchPaths = {}
);

void releaseCachedDevices();

bool isDeviceTypeAvailable(DeviceType deviceType);

bool isSwiftShaderDevice(IDevice* device);

slang::IGlobalSession* getSlangGlobalSession();

const char* getTestsDir();

std::vector<const char*> getSlangSearchPaths();

void initializeRenderDoc();
void renderDocBeginFrame();
void renderDocEndFrame();

template<typename T, typename... Args>
auto makeArray(Args... args)
{
    return std::array<T, sizeof...(Args)>{static_cast<T>(args)...};
}

#define ALL_DEVICE_TYPES                                                                                               \
    {                                                                                                                  \
        rhi::DeviceType::D3D11,                                                                                        \
        rhi::DeviceType::D3D12,                                                                                        \
        rhi::DeviceType::Vulkan,                                                                                       \
        rhi::DeviceType::Metal,                                                                                        \
        rhi::DeviceType::CPU,                                                                                          \
        rhi::DeviceType::CUDA,                                                                                         \
        rhi::DeviceType::WGPU,                                                                                         \
    }

using GpuTestFunc = void (*)(GpuTestContext*, DeviceType);

void runGpuTests(GpuTestFunc func, std::initializer_list<DeviceType> deviceTypes = ALL_DEVICE_TYPES);
void runGpuTestFunc(void (*func)(IDevice* device), int testFlags);

enum TestFlags
{
    // Device type flags
    D3D11 = (1 << (int)DeviceType::D3D11),
    D3D12 = (1 << (int)DeviceType::D3D12),
    Vulkan = (1 << (int)DeviceType::Vulkan),
    Metal = (1 << (int)DeviceType::Metal),
    CPU = (1 << (int)DeviceType::CPU),
    CUDA = (1 << (int)DeviceType::CUDA),
    WGPU = (1 << (int)DeviceType::WGPU),
    ALL = D3D11 | D3D12 | Vulkan | Metal | CPU | CUDA | WGPU,

    // Additional flags
    NoDeviceCache = (1 << 10)
};

} // namespace rhi::testing

#define GPU_TEST_CASE_IMPL(name, func, testFlags)                                                                      \
    static void func(::rhi::IDevice* device);                                                                          \
    TEST_CASE(name)                                                                                                    \
    {                                                                                                                  \
        ::rhi::testing::runGpuTestFunc(func, testFlags);                                                               \
    }                                                                                                                  \
    static void func(::rhi::IDevice* device)

#define GPU_TEST_CASE(name, testFlags) GPU_TEST_CASE_IMPL(name, DOCTEST_ANONYMOUS(GPU_TEST_ANONYMOUS_), testFlags)

#define CHECK_CALL(x) CHECK(!SLANG_FAILED(x))
#define REQUIRE_CALL(x) REQUIRE(!SLANG_FAILED(x))

#define SKIP(reason)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        return;                                                                                                        \
    }                                                                                                                  \
    while (0)

namespace rhi {
inline doctest::String toString(Format value)
{
    return enumToString(value);
}
inline doctest::String toString(TextureType value)
{
    return enumToString(value);
}
} // namespace rhi
