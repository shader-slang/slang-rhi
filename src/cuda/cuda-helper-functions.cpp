#include "cuda-helper-functions.h"
#include "cuda-device.h"

namespace rhi::cuda {

Result CUDAErrorInfo::handle() const
{
    fprintf(stderr, "%s(%d): CUDA: %s (%s)\n", m_filePath, m_lineNo, m_errorString, m_errorName);
    return SLANG_FAIL;
}

Result _handleCUDAError(CUresult cuResult, const char* file, int line)
{
    CUDAErrorInfo info(file, line);
    cuGetErrorString(cuResult, &info.m_errorString);
    cuGetErrorName(cuResult, &info.m_errorName);
    return info.handle();
}

#if SLANG_RHI_ENABLE_OPTIX

#if 1
Result _handleOptixError(OptixResult result, const char* file, int line)
{
    fprintf(stderr, "%s(%d): optix: %s (%s)\n", file, line, optixGetErrorString(result), optixGetErrorName(result));
    return SLANG_FAIL;
}

void _optixLogCallback(unsigned int level, const char* tag, const char* message, void* userData)
{
    fprintf(stderr, "optix: %s (%s)\n", message, tag);
}
#endif
#endif // SLANG_RHI_ENABLE_OPTIX

AdapterLUID getAdapterLUID(int deviceIndex)
{
    CUdevice device;
    SLANG_CUDA_ASSERT_ON_FAIL(cuDeviceGet(&device, deviceIndex));
    AdapterLUID luid = {};
    unsigned int deviceNodeMask;
    SLANG_CUDA_ASSERT_ON_FAIL(cuDeviceGetLuid((char*)&luid, &deviceNodeMask, device));
    return luid;
}

Result SLANG_MCALL getAdapters(std::vector<AdapterInfo>& outAdapters)
{
    int deviceCount;
    SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetCount(&deviceCount));
    for (int deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++)
    {
        CUdevice device;
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGet(&device, deviceIndex));

        AdapterInfo info = {};
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetName(info.name, sizeof(info.name), device));
        info.luid = getAdapterLUID(deviceIndex);
        outAdapters.push_back(info);
    }

    return SLANG_OK;
}

} // namespace rhi::cuda

namespace rhi {

Result SLANG_MCALL getCUDAAdapters(std::vector<AdapterInfo>& outAdapters)
{
    return cuda::getAdapters(outAdapters);
}

Result SLANG_MCALL createCUDADevice(const DeviceDesc* desc, IDevice** outDevice)
{
    RefPtr<cuda::DeviceImpl> result = new cuda::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

} // namespace rhi
