#include "cuda-backend.h"
#include "cuda-device.h"
#include "cuda-utils.h"
#include "../reference.h"

namespace rhi::cuda {

static int calcSMCountPerMultiProcessor(int major, int minor)
{
    // Defines for GPU Architecture types (using the SM version to determine
    // the # of cores per SM
    struct SMInfo
    {
        int sm; // 0xMm (hexadecimal notation), M = SM Major version, and m = SM minor version
        int coreCount;
    };

    static const SMInfo infos[] = {
        {0x30, 192},
        {0x32, 192},
        {0x35, 192},
        {0x37, 192},
        {0x50, 128},
        {0x52, 128},
        {0x53, 128},
        {0x60, 64},
        {0x61, 128},
        {0x62, 128},
        {0x70, 64},
        {0x72, 64},
        {0x75, 64}
    };

    const int sm = ((major << 4) + minor);
    for (size_t i = 0; i < SLANG_COUNT_OF(infos); ++i)
    {
        if (infos[i].sm == sm)
        {
            return infos[i].coreCount;
        }
    }

    const auto& last = infos[SLANG_COUNT_OF(infos) - 1];

    // It must be newer presumably
    SLANG_RHI_ASSERT(sm > last.sm);

    // Default to the last entry
    return last.coreCount;
}

static Result findMaxFlopsDeviceIndex(int* outDeviceIndex)
{
    int smPerMultiproc = 0;
    int maxPerfDevice = -1;
    int deviceCount = 0;

    uint64_t maxComputePerf = 0;
    SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetCount(&deviceCount));

    // Find the best CUDA capable GPU device
    for (int currentDevice = 0; currentDevice < deviceCount; ++currentDevice)
    {
        CUdevice device;
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGet(&device, currentDevice));
        int computeMode = -1, major = 0, minor = 0;
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetAttribute(&computeMode, CU_DEVICE_ATTRIBUTE_COMPUTE_MODE, device));
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device));
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device));

        // If this GPU is not running on Compute Mode prohibited,
        // then we can add it to the list
        if (computeMode != CU_COMPUTEMODE_PROHIBITED)
        {
            if (major == 9999 && minor == 9999)
            {
                smPerMultiproc = 1;
            }
            else
            {
                smPerMultiproc = calcSMCountPerMultiProcessor(major, minor);
            }

            int multiProcessorCount = 0, clockRate = 0;
            SLANG_CUDA_RETURN_ON_FAIL(
                cuDeviceGetAttribute(&multiProcessorCount, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, device)
            );
            SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetAttribute(&clockRate, CU_DEVICE_ATTRIBUTE_CLOCK_RATE, device));
            uint64_t compute_perf = uint64_t(multiProcessorCount) * smPerMultiproc * clockRate;

            if (compute_perf > maxComputePerf)
            {
                maxComputePerf = compute_perf;
                maxPerfDevice = currentDevice;
            }
        }
    }

    if (maxPerfDevice < 0)
    {
        return SLANG_FAIL;
    }

    *outDeviceIndex = maxPerfDevice;
    return SLANG_OK;
}

static Result getAdaptersImpl(std::vector<AdapterImpl>& outAdapters)
{
    if (!rhiCudaDriverApiInit())
    {
        return SLANG_FAIL;
    }

    SLANG_CUDA_RETURN_ON_FAIL(cuInit(0));

    int deviceCount;
    SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetCount(&deviceCount));

    for (int deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++)
    {
        CUdevice device;
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGet(&device, deviceIndex));

        AdapterInfo info = {};
        info.deviceType = DeviceType::CUDA;
        info.adapterType = AdapterType::Discrete;
        SLANG_CUDA_RETURN_ON_FAIL(cuDeviceGetName(info.name, sizeof(info.name), device));
        info.luid = getAdapterLUID(deviceIndex);

        AdapterImpl adapter;
        adapter.m_info = info;
        adapter.m_deviceIndex = deviceIndex;
        outAdapters.push_back(adapter);
    }

    // Find the max flops adapter and mark it as the default one.
    if (!outAdapters.empty())
    {
        int defaultDeviceIndex = 0;
        SLANG_RETURN_ON_FAIL(findMaxFlopsDeviceIndex(&defaultDeviceIndex));
        SLANG_RHI_ASSERT(defaultDeviceIndex >= 0 && defaultDeviceIndex < (int)outAdapters.size());
        outAdapters[defaultDeviceIndex].m_isDefault = true;
    }

    return SLANG_OK;
}

Result BackendImpl::initialize()
{
    return getAdaptersImpl(m_adapters);
}

IAdapter* BackendImpl::getAdapter(uint32_t index)
{
    return index < m_adapters.size() ? &m_adapters[index] : nullptr;
}

Result BackendImpl::createDevice(const DeviceDesc& desc, IDevice** outDevice)
{
    RefPtr<DeviceImpl> result = new DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(desc, this));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

} // namespace rhi::cuda

namespace rhi {

Result createCUDABackend(Backend** outBackend)
{
    RefPtr<cuda::BackendImpl> backend = new cuda::BackendImpl();
    SLANG_RETURN_ON_FAIL(backend->initialize());
    returnRefPtr(outBackend, backend);
    return SLANG_OK;
}

} // namespace rhi
