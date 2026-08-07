#include "testing.h"

#if SLANG_RHI_ENABLE_CUDA

#include "cuda/cuda-nvrtc.h"

using namespace rhi;
using namespace rhi::testing;
using namespace rhi::cuda;

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
