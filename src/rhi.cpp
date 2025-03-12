#include <slang-rhi.h>

#include "debug-layer/debug-device.h"
#include "rhi-shared.h"

#include "core/common.h"
#include "core/task-pool.h"

#include <cstring>
#include <vector>

namespace rhi {

Result SLANG_MCALL createD3D11Device(const DeviceDesc* desc, IDevice** outDevice);
Result SLANG_MCALL createD3D12Device(const DeviceDesc* desc, IDevice** outDevice);
Result SLANG_MCALL createVKDevice(const DeviceDesc* desc, IDevice** outDevice);
Result SLANG_MCALL createMetalDevice(const DeviceDesc* desc, IDevice** outDevice);
Result SLANG_MCALL createCUDADevice(const DeviceDesc* desc, IDevice** outDevice);
Result SLANG_MCALL createCPUDevice(const DeviceDesc* desc, IDevice** outDevice);
Result SLANG_MCALL createWGPUDevice(const DeviceDesc* desc, IDevice** outDevice);

Result SLANG_MCALL getD3D11Adapters(std::vector<AdapterInfo>& outAdapters);
Result SLANG_MCALL getD3D12Adapters(std::vector<AdapterInfo>& outAdapters);
Result SLANG_MCALL getVKAdapters(std::vector<AdapterInfo>& outAdapters);
Result SLANG_MCALL getMetalAdapters(std::vector<AdapterInfo>& outAdapters);
Result SLANG_MCALL getCUDAAdapters(std::vector<AdapterInfo>& outAdapters);

Result SLANG_MCALL reportD3DLiveObjects();
void SLANG_MCALL enableD3D12DebugLayerIfAvailable();

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Global Functions !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */

struct FormatInfoMap
{
    FormatInfoMap()
    {
        // Set all to nothing initially
        for (auto& info : m_infos)
        {
            info.channelCount = 0;
            info.channelType = SLANG_SCALAR_TYPE_NONE;
        }

        m_infos[0].name = "Undefined";

        set(Format::R8Uint, "R8Uint", SLANG_SCALAR_TYPE_UINT8, 1, 1);
        set(Format::R8Sint, "R8Sint", SLANG_SCALAR_TYPE_INT8, 1, 1);
        set(Format::R8Unorm, "R8Unorm", SLANG_SCALAR_TYPE_FLOAT32, 1, 1);
        set(Format::R8Snorm, "R8Snorm", SLANG_SCALAR_TYPE_FLOAT32, 1, 1);

        set(Format::RG8Uint, "RG8Uint", SLANG_SCALAR_TYPE_UINT8, 2, 2);
        set(Format::RG8Sint, "RG8Sint", SLANG_SCALAR_TYPE_INT8, 2, 2);
        set(Format::RG8Unorm, "RG8Unorm", SLANG_SCALAR_TYPE_FLOAT32, 2, 2);
        set(Format::RG8Snorm, "RG8Snorm", SLANG_SCALAR_TYPE_FLOAT32, 2, 2);

        set(Format::RGBA8Uint, "RGBA8Uint", SLANG_SCALAR_TYPE_UINT8, 4, 4);
        set(Format::RGBA8Sint, "RGBA8Sint", SLANG_SCALAR_TYPE_INT8, 4, 4);
        set(Format::RGBA8Unorm, "RGBA8Unorm", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);
        set(Format::RGBA8UnormSrgb, "RGBA8UnormSrgb", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);
        set(Format::RGBA8Snorm, "RGBA8Snorm", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);

        set(Format::BGRA8Unorm, "BGRA8Unorm", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);
        set(Format::BGRA8UnormSrgb, "BGRA8UnormSrgb", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);
        set(Format::BGRX8Unorm, "BGRX8Unorm", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);
        set(Format::BGRX8UnormSrgb, "BGRX8UnormSrgb", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);

        set(Format::R16Uint, "R16Uint", SLANG_SCALAR_TYPE_UINT16, 1, 2);
        set(Format::R16Sint, "R16Sint", SLANG_SCALAR_TYPE_INT16, 1, 2);
        set(Format::R16Unorm, "R16Unorm", SLANG_SCALAR_TYPE_FLOAT32, 1, 2);
        set(Format::R16Snorm, "R16Snorm", SLANG_SCALAR_TYPE_FLOAT32, 1, 2);
        set(Format::R16Float, "R16Float", SLANG_SCALAR_TYPE_FLOAT16, 1, 2);

        set(Format::RG16Uint, "RG16Uint", SLANG_SCALAR_TYPE_UINT16, 2, 4);
        set(Format::RG16Sint, "RG16Sint", SLANG_SCALAR_TYPE_INT16, 2, 4);
        set(Format::RG16Unorm, "RG16Unorm", SLANG_SCALAR_TYPE_FLOAT32, 2, 4);
        set(Format::RG16Snorm, "RG16Snorm", SLANG_SCALAR_TYPE_FLOAT32, 2, 4);
        set(Format::RG16Float, "RG16Float", SLANG_SCALAR_TYPE_FLOAT16, 2, 4);

        set(Format::RGBA16Uint, "RGBA16Uint", SLANG_SCALAR_TYPE_UINT16, 4, 8);
        set(Format::RGBA16Sint, "RGBA16Sint", SLANG_SCALAR_TYPE_INT16, 4, 8);
        set(Format::RGBA16Unorm, "RGBA16Unorm", SLANG_SCALAR_TYPE_FLOAT32, 4, 8);
        set(Format::RGBA16Snorm, "RGBA16Snorm", SLANG_SCALAR_TYPE_FLOAT32, 4, 8);
        set(Format::RGBA16Float, "RGBA16Float", SLANG_SCALAR_TYPE_FLOAT16, 4, 8);

        set(Format::R32Uint, "R32Uint", SLANG_SCALAR_TYPE_UINT32, 1, 4);
        set(Format::R32Sint, "R32Sint", SLANG_SCALAR_TYPE_INT32, 1, 4);
        set(Format::R32Float, "R32Float", SLANG_SCALAR_TYPE_FLOAT32, 1, 4);

        set(Format::RG32Uint, "RG32Uint", SLANG_SCALAR_TYPE_UINT32, 2, 8);
        set(Format::RG32Sint, "RG32Sint", SLANG_SCALAR_TYPE_INT32, 2, 8);
        set(Format::RG32Float, "RG32Float", SLANG_SCALAR_TYPE_FLOAT32, 2, 8);

        set(Format::RGB32Uint, "RGB32Uint", SLANG_SCALAR_TYPE_UINT32, 3, 12);
        set(Format::RGB32Sint, "RGB32Sint", SLANG_SCALAR_TYPE_INT32, 3, 12);
        set(Format::RGB32Float, "RGB32Float", SLANG_SCALAR_TYPE_FLOAT32, 3, 12);

        set(Format::RGBA32Uint, "RGBA32Uint", SLANG_SCALAR_TYPE_UINT32, 4, 16);
        set(Format::RGBA32Sint, "RGBA32Sint", SLANG_SCALAR_TYPE_INT32, 4, 16);
        set(Format::RGBA32Float, "RGBA32Float", SLANG_SCALAR_TYPE_FLOAT32, 4, 16);

        set(Format::R64Uint, "R64Uint", SLANG_SCALAR_TYPE_UINT64, 1, 8);
        set(Format::R64Sint, "R64Sint", SLANG_SCALAR_TYPE_INT64, 1, 8);

        set(Format::BGRA4Unorm, "BGRA4Unorm", SLANG_SCALAR_TYPE_FLOAT32, 4, 2);
        set(Format::B5G6R5Unorm, "B5G6R5Unorm", SLANG_SCALAR_TYPE_FLOAT32, 3, 2);
        set(Format::BGR5A1Unorm, "BGR5A1Unorm", SLANG_SCALAR_TYPE_FLOAT32, 4, 2);

        set(Format::RGB9E5Ufloat, "RGB9E5Ufloat", SLANG_SCALAR_TYPE_FLOAT32, 3, 4);
        set(Format::RGB10A2Uint, "RGB10A2Uint", SLANG_SCALAR_TYPE_UINT32, 4, 4);
        set(Format::RGB10A2Unorm, "RGB10A2Unorm", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);
        set(Format::R11G11B10Float, "R11G11B10Float", SLANG_SCALAR_TYPE_FLOAT32, 3, 4);

        set(Format::D32Float, "D32Float", SLANG_SCALAR_TYPE_FLOAT32, 1, 4);
        set(Format::D16Unorm, "D16Unorm", SLANG_SCALAR_TYPE_FLOAT32, 1, 2);
        set(Format::D32FloatS8Uint, "D32FloatS8Uint", SLANG_SCALAR_TYPE_FLOAT32, 2, 8);

        set(Format::BC1Unorm, "BC1Unorm", SLANG_SCALAR_TYPE_FLOAT32, 4, 8, 16, 4, 4);
        set(Format::BC1UnormSrgb, "BC1UnormSrgb", SLANG_SCALAR_TYPE_FLOAT32, 4, 8, 16, 4, 4);
        set(Format::BC2Unorm, "BC2Unorm", SLANG_SCALAR_TYPE_FLOAT32, 4, 16, 16, 4, 4);
        set(Format::BC2UnormSrgb, "BC2UnormSrgb", SLANG_SCALAR_TYPE_FLOAT32, 4, 16, 16, 4, 4);
        set(Format::BC3Unorm, "BC3Unorm", SLANG_SCALAR_TYPE_FLOAT32, 4, 16, 16, 4, 4);
        set(Format::BC3UnormSrgb, "BC3UnormSrgb", SLANG_SCALAR_TYPE_FLOAT32, 4, 16, 16, 4, 4);
        set(Format::BC4Unorm, "BC4Unorm", SLANG_SCALAR_TYPE_FLOAT32, 1, 8, 16, 4, 4);
        set(Format::BC4Snorm, "BC4Snorm", SLANG_SCALAR_TYPE_FLOAT32, 1, 8, 16, 4, 4);
        set(Format::BC5Unorm, "BC5Unorm", SLANG_SCALAR_TYPE_FLOAT32, 2, 16, 16, 4, 4);
        set(Format::BC5Snorm, "BC5Snorm", SLANG_SCALAR_TYPE_FLOAT32, 2, 16, 16, 4, 4);
        set(Format::BC6HUfloat, "BC6HUfloat", SLANG_SCALAR_TYPE_FLOAT32, 3, 16, 16, 4, 4);
        set(Format::BC6HSfloat, "BC6HSfloat", SLANG_SCALAR_TYPE_FLOAT32, 3, 16, 16, 4, 4);
        set(Format::BC7Unorm, "BC7Unorm", SLANG_SCALAR_TYPE_FLOAT32, 4, 16, 16, 4, 4);
        set(Format::BC7UnormSrgb, "BC7UnormSrgb", SLANG_SCALAR_TYPE_FLOAT32, 4, 16, 16, 4, 4);
    }

    bool isCompressed(Format format)
    {
        switch (format)
        {
        case Format::BC1Unorm:
        case Format::BC1UnormSrgb:
        case Format::BC2Unorm:
        case Format::BC2UnormSrgb:
        case Format::BC3Unorm:
        case Format::BC3UnormSrgb:
        case Format::BC4Unorm:
        case Format::BC4Snorm:
        case Format::BC5Unorm:
        case Format::BC5Snorm:
        case Format::BC6HUfloat:
        case Format::BC6HSfloat:
        case Format::BC7Unorm:
        case Format::BC7UnormSrgb:
            return true;
        default:
            return false;
        }
    }

    void set(
        Format format,
        const char* name,
        SlangScalarType type,
        uint8_t channelCount,
        uint8_t blockSizeInBytes,
        uint8_t pixelsPerBlock = 1,
        uint8_t blockWidth = 1,
        uint8_t blockHeight = 1
    )
    {
        FormatInfo& info = m_infos[size_t(format)];
        info.name = name;
        info.channelCount = channelCount;
        info.channelType = uint8_t(type);

        info.blockSizeInBytes = blockSizeInBytes;
        info.pixelsPerBlock = pixelsPerBlock;
        info.blockWidth = blockWidth;
        info.blockHeight = blockHeight;

        info.hasDepth = isDepthFormat(format);
        info.hasStencil = isStencilFormat(format);
        info.isCompressed = isCompressed(format);
    }

    const FormatInfo& get(Format format) const { return m_infos[size_t(format)]; }

    FormatInfo m_infos[size_t(Format::_Count)];
};

static const FormatInfoMap s_formatInfoMap;

class RHI : public IRHI
{
public:
    bool debugLayersEnabled = false;

    virtual const FormatInfo& getFormatInfo(Format format) override { return s_formatInfoMap.get(format); }
    virtual const char* getDeviceTypeName(DeviceType type) override;
    virtual bool isDeviceTypeSupported(DeviceType type) override;

    Result getAdapters(DeviceType type, ISlangBlob** outAdaptersBlob) override;
    Result createDevice(const DeviceDesc& desc, IDevice** outDevice) override;
    void enableDebugLayers() override;
    Result reportLiveObjects() override;
    Result setTaskPoolWorkerCount(uint32_t count) override;
    Result setTaskScheduler(ITaskScheduler* scheduler) override;

    static RHI* getInstance()
    {
        static RHI instance;
        return &instance;
    }
};

const char* RHI::getDeviceTypeName(DeviceType type)
{
    switch (type)
    {
    case DeviceType::Default:
        return "Default";
    case DeviceType::D3D11:
        return "D3D11";
    case DeviceType::D3D12:
        return "D3D12";
    case DeviceType::Vulkan:
        return "Vulkan";
    case DeviceType::Metal:
        return "Metal";
    case DeviceType::CPU:
        return "CPU";
    case DeviceType::CUDA:
        return "CUDA";
    case DeviceType::WGPU:
        return "WGPU";
    }
    return "invalid";
}

bool RHI::isDeviceTypeSupported(DeviceType type)
{
    switch (type)
    {
    case DeviceType::D3D11:
        return SLANG_RHI_ENABLE_D3D11;
    case DeviceType::D3D12:
        return SLANG_RHI_ENABLE_D3D12;
    case DeviceType::Vulkan:
        return SLANG_RHI_ENABLE_VULKAN;
    case DeviceType::Metal:
        return SLANG_RHI_ENABLE_METAL;
    case DeviceType::CPU:
        return SLANG_RHI_ENABLE_CPU;
    case DeviceType::CUDA:
        return SLANG_RHI_ENABLE_CUDA;
    case DeviceType::WGPU:
        return SLANG_RHI_ENABLE_WGPU;
    default:
        return false;
    }
}

Result RHI::getAdapters(DeviceType type, ISlangBlob** outAdaptersBlob)
{
    std::vector<AdapterInfo> adapters;

    switch (type)
    {
#if SLANG_RHI_ENABLE_D3D11
    case DeviceType::D3D11:
        SLANG_RETURN_ON_FAIL(getD3D11Adapters(adapters));
        break;
#endif
#if SLANG_RHI_ENABLE_D3D12
    case DeviceType::D3D12:
        SLANG_RETURN_ON_FAIL(getD3D12Adapters(adapters));
        break;
#endif
#if SLANG_RHI_ENABLE_VULKAN
    case DeviceType::Vulkan:
        SLANG_RETURN_ON_FAIL(getVKAdapters(adapters));
        break;
#endif
#if SLANG_RHI_ENABLE_METAL
    case DeviceType::Metal:
        SLANG_RETURN_ON_FAIL(getMetalAdapters(adapters));
        break;
#endif
    case DeviceType::CPU:
        return SLANG_E_NOT_IMPLEMENTED;
#if SLANG_RHI_ENABLE_CUDA
    case DeviceType::CUDA:
        SLANG_RETURN_ON_FAIL(getCUDAAdapters(adapters));
        break;
#endif
    default:
        return SLANG_E_INVALID_ARG;
    }

    auto adaptersBlob = OwnedBlob::create(adapters.data(), adapters.size() * sizeof(AdapterInfo));
    if (outAdaptersBlob)
        returnComPtr(outAdaptersBlob, adaptersBlob);

    return SLANG_OK;
}

inline Result _createDevice(const DeviceDesc* desc, IDevice** outDevice)
{
    switch (desc->deviceType)
    {
    case DeviceType::Default:
    {
        DeviceDesc newDesc = *desc;
#if SLANG_WINDOWS_FAMILY
        newDesc.deviceType = DeviceType::D3D12;
        if (_createDevice(&newDesc, outDevice) == SLANG_OK)
            return SLANG_OK;
        newDesc.deviceType = DeviceType::Vulkan;
        if (_createDevice(&newDesc, outDevice) == SLANG_OK)
            return SLANG_OK;
#elif SLANG_LINUX_FAMILY
        newDesc.deviceType = DeviceType::Vulkan;
        if (_createDevice(&newDesc, outDevice) == SLANG_OK)
            return SLANG_OK;
#elif SLANG_APPLE_FAMILY
        newDesc.deviceType = DeviceType::Metal;
        if (_createDevice(&newDesc, outDevice) == SLANG_OK)
            return SLANG_OK;
#endif
        return SLANG_FAIL;
    }
    break;
#if SLANG_RHI_ENABLE_D3D11
    case DeviceType::D3D11:
    {
        return createD3D11Device(desc, outDevice);
    }
#endif
#if SLANG_RHI_ENABLE_D3D12
    case DeviceType::D3D12:
    {
        return createD3D12Device(desc, outDevice);
    }
#endif
#if SLANG_RHI_ENABLE_VULKAN
    case DeviceType::Vulkan:
    {
        return createVKDevice(desc, outDevice);
    }
#endif
#if SLANG_RHI_ENABLE_METAL
    case DeviceType::Metal:
    {
        return createMetalDevice(desc, outDevice);
    }
#endif
#if SLANG_RHI_ENABLE_CUDA
    case DeviceType::CUDA:
    {
        return createCUDADevice(desc, outDevice);
    }
#endif
#if SLANG_RHI_ENABLE_CPU
    case DeviceType::CPU:
    {
        return createCPUDevice(desc, outDevice);
    }
#endif
#if SLANG_RHI_ENABLE_WGPU
    case DeviceType::WGPU:
    {
        return createWGPUDevice(desc, outDevice);
    }
#endif
    default:
        return SLANG_FAIL;
    }
}

void RHI::enableDebugLayers()
{
    if (debugLayersEnabled)
        return;
#if SLANG_RHI_ENABLE_D3D12
    enableD3D12DebugLayerIfAvailable();
#endif
    debugLayersEnabled = true;
}

Result RHI::createDevice(const DeviceDesc& desc, IDevice** outDevice)
{
    ComPtr<IDevice> innerDevice;
    auto resultCode = _createDevice(&desc, innerDevice.writeRef());
    if (SLANG_FAILED(resultCode))
        return resultCode;
    if (!desc.enableValidation)
    {
        returnComPtr(outDevice, innerDevice);
        return resultCode;
    }
    IDebugCallback* debugCallback = checked_cast<Device*>(innerDevice.get())->m_debugCallback;
    RefPtr<debug::DebugDevice> debugDevice =
        new debug::DebugDevice(innerDevice->getDeviceInfo().deviceType, debugCallback);
    debugDevice->baseObject = innerDevice;
    returnComPtr(outDevice, debugDevice);
    return resultCode;
}

Result RHI::reportLiveObjects()
{
#if SLANG_RHI_ENABLE_D3D11 | SLANG_RHI_ENABLE_D3D12
    SLANG_RETURN_ON_FAIL(reportD3DLiveObjects());
#endif
    return SLANG_OK;
}

Result RHI::setTaskPoolWorkerCount(uint32_t count)
{
    return setGlobalTaskPoolWorkerCount(count);
}

Result RHI::setTaskScheduler(ITaskScheduler* scheduler)
{
    return setGlobalTaskScheduler(scheduler);
}

bool isDebugLayersEnabled()
{
    return RHI::getInstance()->debugLayersEnabled;
}

extern "C"
{
    IRHI* getRHI()
    {
        return RHI::getInstance();
    }
}

} // namespace rhi
