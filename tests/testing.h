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

struct Options
{
    bool verbose = false;
    bool checkDevices = false;
};

inline Options& options()
{
    static Options opts;
    return opts;
}

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
    DeviceType deviceType;
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

Result loadRenderProgramFromSource(
    IDevice* device,
    ComPtr<IShaderProgram>& outShaderProgram,
    std::string_view source,
    const char* vertexEntryPointName,
    const char* fragmentEntryPointName
);

template<typename T>
void compareResult(const T* result, const T* expectedResult, size_t count, bool expectFailure = false)
{
    if (expectFailure)
    {
        size_t mismatchCount = 0;
        for (size_t i = 0; i < count; i++)
        {
            if (result[i] != expectedResult[i])
            {
                mismatchCount++;
            }
        }
        CHECK_GT(mismatchCount, 0);
    }
    else
    {
        for (size_t i = 0; i < count; i++)
        {
            CAPTURE(i);
            CHECK_EQ(result[i], expectedResult[i]);
        }
    }
}

inline void compareResultFuzzy(
    const float* result,
    const float* expectedResult,
    size_t count,
    bool expectFailure = false
)
{
    if (expectFailure)
    {
        size_t mismatchCount = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (result[i] > expectedResult[i] + 0.01f || result[i] < expectedResult[i] - 0.01f)
            {
                mismatchCount++;
            }
        }
        CHECK_GT(mismatchCount, 0);
    }
    else
    {
        for (size_t i = 0; i < count; ++i)
        {
            CAPTURE(i);
            CHECK_LE(result[i], expectedResult[i] + 0.01f);
            CHECK_GE(result[i], expectedResult[i] - 0.01f);
        }
    }
}

template<typename T>
void compareComputeResult(IDevice* device, IBuffer* buffer, span<T> expectedResult, bool expectFailure = false)
{
    size_t bufferSize = expectedResult.size() * sizeof(T);
    // Read back the results.
    ComPtr<ISlangBlob> bufferData;
    REQUIRE(SLANG_SUCCEEDED(device->readBuffer(buffer, 0, bufferSize, bufferData.writeRef())));
    REQUIRE(bufferData->getBufferSize() == bufferSize);
    const T* result = reinterpret_cast<const T*>(bufferData->getBufferPointer());

    if constexpr (std::is_same<T, float>::value)
        compareResultFuzzy(result, expectedResult.data(), expectedResult.size(), expectFailure);
    else
        compareResult<T>(result, expectedResult.data(), expectedResult.size(), expectFailure);
}

template<typename T, size_t Count>
void compareComputeResult(
    IDevice* device,
    IBuffer* buffer,
    std::array<T, Count> expectedResult,
    bool expectFailure = false
)
{
    compareComputeResult(device, buffer, span<T>(expectedResult.data(), Count), expectFailure);
}

template<typename T>
void compareComputeResult(
    IDevice* device,
    ITexture* texture,
    uint32_t layer,
    uint32_t mip,
    span<T> expectedResult,
    bool expectFailure = false
)
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
        compareResultFuzzy(result, expectedResult.data(), expectedResult.size(), expectFailure);
    else
        compareResult<T>(result, expectedResult.data(), expectedResult.size(), expectFailure);
}

template<typename T, size_t Count>
void compareComputeResult(
    IDevice* device,
    ITexture* texture,
    uint32_t layer,
    uint32_t mip,
    std::array<T, Count> expectedResult,
    bool expectFailure = false
)
{
    compareComputeResult(device, texture, layer, mip, span<T>(expectedResult.data(), Count), expectFailure);
}

struct DeviceExtraOptions
{
    std::vector<const char*> searchPaths;
    IPersistentCache* persistentShaderCache = nullptr;
    IPersistentCache* persistentPipelineCache = nullptr;
    bool enableCompilationReports = false;
    DeviceNativeHandles existingDeviceHandles;
};

ComPtr<IDevice> createTestingDevice(
    GpuTestContext* ctx,
    DeviceType deviceType,
    bool useCachedDevice = true,
    const DeviceExtraOptions* extraOptions = nullptr
);

void releaseCachedDevices();

struct DeviceAvailabilityResult
{
    bool available;
    std::string error;
    std::string debugCallbackOutput;
    std::string diagnostics;
    ComPtr<IDevice> device;
};

DeviceAvailabilityResult checkDeviceTypeAvailable(DeviceType deviceType);

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

static constexpr DeviceType kPlatformDeviceTypes[] = {
#if SLANG_WINDOWS_FAMILY
    rhi::DeviceType::D3D11,
    rhi::DeviceType::D3D12,
    rhi::DeviceType::Vulkan,
    rhi::DeviceType::CPU,
    rhi::DeviceType::CUDA,
    rhi::DeviceType::WGPU,
#elif SLANG_LINUX_FAMILY
    rhi::DeviceType::Vulkan,
    rhi::DeviceType::CPU,
    rhi::DeviceType::CUDA,
    rhi::DeviceType::WGPU,
#elif SLANG_APPLE_FAMILY
    rhi::DeviceType::Vulkan,
    rhi::DeviceType::Metal,
    rhi::DeviceType::CPU,
    rhi::DeviceType::CUDA,
    rhi::DeviceType::WGPU,
#endif
};

inline bool isPlatformDeviceType(DeviceType deviceType)
{
    for (DeviceType platformDeviceType : kPlatformDeviceTypes)
    {
        if (platformDeviceType == deviceType)
        {
            return true;
        }
    }
    return false;
}

using GpuTestFunc = void (*)(GpuTestContext*, ComPtr<IDevice>);

enum GpuTestFlags
{
    None = 0,

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
    DontCreateDevice = (1 << 10), // Do not create a device (device argument is nullptr)
    DontCacheDevice = (1 << 11),  // Do not use cached devices (create a new device for this test case)
};

struct GpuTestInfo
{
    GpuTestFunc func;
    DeviceType deviceType;
    GpuTestFlags flags;
};
static_assert(std::is_pod_v<GpuTestInfo>, "GpuTestInfo must be POD");

int registerGpuTest(const char* name, GpuTestFunc func, GpuTestFlags flags, const char* file, int line);

void reportSkip(const doctest::detail::TestCase* tc, const char* reason);
const char* getSkipMessage(const doctest::TestCaseData* tc);

} // namespace rhi::testing

#define GPU_TEST_CASE_IMPL(name, func, flags)                                                                          \
    static void func(::rhi::testing::GpuTestContext* ctx, ::ComPtr<::rhi::IDevice> device);                            \
    DOCTEST_GLOBAL_NO_WARNINGS(                                                                                        \
        DOCTEST_ANONYMOUS(DOCTEST_ANON_VAR_),                                                                          \
        ::rhi::testing::registerGpuTest(                                                                               \
            name,                                                                                                      \
            func,                                                                                                      \
            static_cast<::rhi::testing::GpuTestFlags>(flags),                                                          \
            __FILE__,                                                                                                  \
            __LINE__                                                                                                   \
        )                                                                                                              \
    )                                                                                                                  \
    static void func(::rhi::testing::GpuTestContext* ctx, ::ComPtr<::rhi::IDevice> device)

// Register a GPU test case.
// This will register one test case for each device type specified in the flags.
// Each test will be named <name>.<deviceType> where <deviceType> is the string representation of the device type.
// The GPU test function has the following signature: void func(GpuTestContext* ctx, ComPtr<IDevice> device)
// In addition to the device flags, the following flags can be used:
// - GpuTestFlag::DontCreateDevice: Do not create a device (device argument is nullptr)
// - GpuTestFlag::DontCacheDevice: Do not use cached devices (create a new device for this test case)
#define GPU_TEST_CASE(name, flags) GPU_TEST_CASE_IMPL(name, DOCTEST_ANONYMOUS(GPU_TEST_ANONYMOUS_), flags)

#define CHECK_CALL(x) CHECK(!SLANG_FAILED(x))
#define REQUIRE_CALL(x) REQUIRE(!SLANG_FAILED(x))

// doctest does not support skipping tests at runtime.
// We add this functionality using this SKIP macro which should only be called in the main scope of the test function.
// The `reason` argument MUST be a string literal.
#define SKIP(reason)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        ::rhi::testing::reportSkip(::doctest::getContextOptions()->currentTest, "" reason);                            \
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
