#include "testing.h"

#if SLANG_RHI_ENABLE_CUDA

#include "cuda/cuda-nvrtc.h"
#include "cuda/cuda-api.h"
#include "debug-layer/debug-device.h"

using namespace rhi;
using namespace rhi::testing;
using namespace rhi::cuda;

TEST_CASE("nvrtc-capability-query")
{
    NVRTC compiler;
    compiler.nvrtcVersion = [](int* major, int* minor)
    {
        *major = 13;
        *minor = 1;
        return NVRTC_SUCCESS;
    };
    CUDACompilerInfo info;
    std::vector<uint32_t> architectures;
    REQUIRE(compiler.queryCompilerInfo(info, architectures) == SLANG_OK);
    CHECK_FALSE(info.architectureQueryAvailable);
    CHECK(info.versionMajor == 13);
    compiler.nvrtcGetNumSupportedArchs = [](int* count)
    {
        *count = 3;
        return NVRTC_SUCCESS;
    };
    compiler.nvrtcGetSupportedArchs = [](int* values)
    {
        values[0] = 75;
        values[1] = 90;
        values[2] = 130;
        return NVRTC_SUCCESS;
    };
    REQUIRE(compiler.queryCompilerInfo(info, architectures) == SLANG_OK);
    CHECK(info.architectureQueryAvailable);
    CHECK(architectures == std::vector<uint32_t>{75, 90, 130});
    CHECK(info.supportedArchitectureCount == 3);
    CHECK(info.supportedArchitectures == architectures.data());
    compiler.nvrtcGetNumSupportedArchs = [](int* count)
    {
        *count = 0;
        return NVRTC_SUCCESS;
    };
    REQUIRE(compiler.queryCompilerInfo(info, architectures) == SLANG_OK);
    CHECK(info.architectureQueryAvailable);
    CHECK(architectures.empty());
    compiler.nvrtcGetNumSupportedArchs = [](int*)
    {
        return NVRTC_ERROR_INTERNAL_ERROR;
    };
    CHECK(compiler.queryCompilerInfo(info, architectures) == SLANG_FAIL);
    compiler.nvrtcGetNumSupportedArchs = [](int* count)
    {
        *count = 1;
        return NVRTC_SUCCESS;
    };
    compiler.nvrtcGetSupportedArchs = [](int*)
    {
        return NVRTC_ERROR_INTERNAL_ERROR;
    };
    CHECK(compiler.queryCompilerInfo(info, architectures) == SLANG_FAIL);
    compiler.nvrtcVersion = [](int*, int*)
    {
        return NVRTC_ERROR_INTERNAL_ERROR;
    };
    CHECK(compiler.queryCompilerInfo(info, architectures) == SLANG_FAIL);
}

GPU_TEST_CASE("cuda-compiler-info", CUDA)
{
    CUDACompilerInfo info;
    REQUIRE(device->getCUDACompilerInfo(&info) == SLANG_OK);
    REQUIRE(info.path);
    DeviceNativeHandles handles;
    REQUIRE_CALL(device->getNativeDeviceHandles(&handles));
    REQUIRE(handles.handles[0].type == NativeHandleType::CUdevice);
    auto cudaDevice = static_cast<CUdevice>(handles.handles[0].value);
    int major = 0;
    int minor = 0;
    REQUIRE(cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, cudaDevice) == CUDA_SUCCESS);
    REQUIRE(cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, cudaDevice) == CUDA_SUCCESS);
    CHECK(device->getInfo().cudaComputeCapability == static_cast<uint32_t>(major * 10 + minor));
    NVRTC compiler;
    REQUIRE(compiler.initializeQuery(info.path) == SLANG_OK);
    CUDACompilerInfo direct;
    std::vector<uint32_t> architectures;
    REQUIRE(compiler.queryCompilerInfo(direct, architectures) == SLANG_OK);
    CHECK(info.versionMajor == direct.versionMajor);
    CHECK(info.versionMinor == direct.versionMinor);
    CHECK(info.architectureQueryAvailable == direct.architectureQueryAvailable);
    REQUIRE(info.supportedArchitectureCount == architectures.size());
    for (size_t i = 0; i < architectures.size(); ++i)
        CHECK(info.supportedArchitectures[i] == architectures[i]);
    CUDACompilerInfo repeated;
    REQUIRE(device->getCUDACompilerInfo(&repeated) == SLANG_OK);
    CHECK(info.path == repeated.path);
    CHECK(info.supportedArchitectures == repeated.supportedArchitectures);
    struct Callback : IDebugCallback
    {
        bool reportedError = false;
        void handleMessage(DebugMessageType type, DebugMessageSource source, const char* message) override
        {
            reportedError = type == DebugMessageType::Error && source == DebugMessageSource::Layer &&
                            std::strstr(message, "'outInfo' must not be null.");
        }
    } callback;
    debug::DebugDevice debugDevice(DeviceType::CUDA, &callback);
    CHECK(debugDevice.getCUDACompilerInfo(nullptr) == SLANG_E_INVALID_ARG);
    CHECK(callback.reportedError);
}

TEST_CASE("nvrtc")
{
    NVRTC nvrtc;
    if (nvrtc.initialize() != SLANG_OK)
    {
        SKIP("nvrtc not found");
    }

    SUBCASE("compile")
    {
        const char* source = R"(
            #include <cuda_runtime.h>
            extern "C" __global__ void dummyKernel() {
                int idx = threadIdx.x;
            }
        )";

        NVRTC::CompileResult compileResult;
        Result result = nvrtc.compilePTX(source, compileResult);
        CHECK(result == SLANG_OK);
        CHECK(compileResult.result == NVRTC_SUCCESS);
        CHECK(compileResult.ptx.size() > 0);
    }

    SUBCASE("compile-error")
    {
        const char* source = R"(
            #include <cuda_runtime.h>
            extern "C" __global__ void dummyKernel() {
                int idx = threadIdx.x
            }
        )";

        NVRTC::CompileResult compileResult;
        Result result = nvrtc.compilePTX(source, compileResult);
        CHECK(result == SLANG_FAIL);
        CHECK(compileResult.result == NVRTC_ERROR_COMPILATION);
        CHECK(compileResult.ptx.size() == 0);
        CHECK(compileResult.log.size() > 0);
    }
}

#endif // SLANG_RHI_ENABLE_CUDA
