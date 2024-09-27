#include <slang-rhi.h>

#include "debug-layer/debug-device.h"
#include "rhi-shared.h"
#if SLANG_RHI_ENABLE_CUDA
#include "cuda/cuda-api.h"
#endif

#include "core/common.h"

#include <cstring>
#include <vector>

namespace rhi {

Result SLANG_MCALL createD3D11Device(const IDevice::Desc* desc, IDevice** outDevice);
Result SLANG_MCALL createD3D12Device(const IDevice::Desc* desc, IDevice** outDevice);
Result SLANG_MCALL createVKDevice(const IDevice::Desc* desc, IDevice** outDevice);
Result SLANG_MCALL createMetalDevice(const IDevice::Desc* desc, IDevice** outDevice);
Result SLANG_MCALL createCUDADevice(const IDevice::Desc* desc, IDevice** outDevice);
Result SLANG_MCALL createCPUDevice(const IDevice::Desc* desc, IDevice** outDevice);
Result SLANG_MCALL createWGPUDevice(const IDevice::Desc* desc, IDevice** outDevice);

Result SLANG_MCALL getD3D11Adapters(std::vector<AdapterInfo>& outAdapters);
Result SLANG_MCALL getD3D12Adapters(std::vector<AdapterInfo>& outAdapters);
Result SLANG_MCALL getVKAdapters(std::vector<AdapterInfo>& outAdapters);
Result SLANG_MCALL getMetalAdapters(std::vector<AdapterInfo>& outAdapters);
Result SLANG_MCALL getCUDAAdapters(std::vector<AdapterInfo>& outAdapters);

Result SLANG_MCALL reportD3DLiveObjects();

static bool debugLayerEnabled = false;
bool isRhiDebugLayerEnabled()
{
    return debugLayerEnabled;
}

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

        m_infos[0].name = "Unknown";

        set(Format::R32G32B32A32_TYPELESS, "R32G32B32A32_TYPELESS", SLANG_SCALAR_TYPE_UINT32, 4, 16);
        set(Format::R32G32B32_TYPELESS, "R32G32B32_TYPELESS", SLANG_SCALAR_TYPE_UINT32, 3, 12);
        set(Format::R32G32_TYPELESS, "R32G32_TYPELESS", SLANG_SCALAR_TYPE_UINT32, 2, 8);
        set(Format::R32_TYPELESS, "R32_TYPELESS", SLANG_SCALAR_TYPE_UINT32, 1, 4);

        set(Format::R16G16B16A16_TYPELESS, "R16G16B16A16_TYPELESS", SLANG_SCALAR_TYPE_UINT16, 4, 8);
        set(Format::R16G16_TYPELESS, "R16G16_TYPELESS", SLANG_SCALAR_TYPE_UINT16, 2, 4);
        set(Format::R16_TYPELESS, "R16_TYPELESS", SLANG_SCALAR_TYPE_UINT16, 1, 2);

        set(Format::R8G8B8A8_TYPELESS, "R8G8B8A8_TYPELESS", SLANG_SCALAR_TYPE_UINT8, 4, 4);
        set(Format::R8G8_TYPELESS, "R8G8_TYPELESS", SLANG_SCALAR_TYPE_UINT8, 2, 2);
        set(Format::R8_TYPELESS, "R8_TYPELESS", SLANG_SCALAR_TYPE_UINT8, 1, 1);
        set(Format::B8G8R8A8_TYPELESS, "B8G8R8A8_TYPELESS", SLANG_SCALAR_TYPE_UINT8, 4, 4);

        set(Format::R32G32B32A32_FLOAT, "R32G32B32A32_FLOAT", SLANG_SCALAR_TYPE_FLOAT32, 4, 16);
        set(Format::R32G32B32_FLOAT, "R32G32B32_FLOAT", SLANG_SCALAR_TYPE_FLOAT32, 3, 12);
        set(Format::R32G32_FLOAT, "R32G32_FLOAT", SLANG_SCALAR_TYPE_FLOAT32, 2, 8);
        set(Format::R32_FLOAT, "R32_FLOAT", SLANG_SCALAR_TYPE_FLOAT32, 1, 4);

        set(Format::R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT", SLANG_SCALAR_TYPE_FLOAT16, 4, 8);
        set(Format::R16G16_FLOAT, "R16G16_FLOAT", SLANG_SCALAR_TYPE_FLOAT16, 2, 4);
        set(Format::R16_FLOAT, "R16_FLOAT", SLANG_SCALAR_TYPE_FLOAT16, 1, 2);

        set(Format::R64_UINT, "R64_UINT", SLANG_SCALAR_TYPE_UINT64, 1, 8);

        set(Format::R32G32B32A32_UINT, "R32G32B32A32_UINT", SLANG_SCALAR_TYPE_UINT32, 4, 16);
        set(Format::R32G32B32_UINT, "R32G32B32_UINT", SLANG_SCALAR_TYPE_UINT32, 3, 12);
        set(Format::R32G32_UINT, "R32G32_UINT", SLANG_SCALAR_TYPE_UINT32, 2, 8);
        set(Format::R32_UINT, "R32_UINT", SLANG_SCALAR_TYPE_UINT32, 1, 4);

        set(Format::R16G16B16A16_UINT, "R16G16B16A16_UINT", SLANG_SCALAR_TYPE_UINT16, 4, 8);
        set(Format::R16G16_UINT, "R16G16_UINT", SLANG_SCALAR_TYPE_UINT16, 2, 4);
        set(Format::R16_UINT, "R16_UINT", SLANG_SCALAR_TYPE_UINT16, 1, 2);

        set(Format::R8G8B8A8_UINT, "R8G8B8A8_UINT", SLANG_SCALAR_TYPE_UINT8, 4, 4);
        set(Format::R8G8_UINT, "R8G8_UINT", SLANG_SCALAR_TYPE_UINT8, 2, 2);
        set(Format::R8_UINT, "R8_UINT", SLANG_SCALAR_TYPE_UINT8, 1, 1);

        set(Format::R64_SINT, "R64_SINT", SLANG_SCALAR_TYPE_INT64, 1, 8);

        set(Format::R32G32B32A32_SINT, "R32G32B32A32_SINT", SLANG_SCALAR_TYPE_INT32, 4, 16);
        set(Format::R32G32B32_SINT, "R32G32B32_SINT", SLANG_SCALAR_TYPE_INT32, 3, 12);
        set(Format::R32G32_SINT, "R32G32_SINT", SLANG_SCALAR_TYPE_INT32, 2, 8);
        set(Format::R32_SINT, "R32_SINT", SLANG_SCALAR_TYPE_INT32, 1, 4);

        set(Format::R16G16B16A16_SINT, "R16G16B16A16_SINT", SLANG_SCALAR_TYPE_INT16, 4, 8);
        set(Format::R16G16_SINT, "R16G16_SINT", SLANG_SCALAR_TYPE_INT16, 2, 4);
        set(Format::R16_SINT, "R16_SINT", SLANG_SCALAR_TYPE_INT16, 1, 2);

        set(Format::R8G8B8A8_SINT, "R8G8B8A8_SINT", SLANG_SCALAR_TYPE_INT8, 4, 4);
        set(Format::R8G8_SINT, "R8G8_SINT", SLANG_SCALAR_TYPE_INT8, 2, 2);
        set(Format::R8_SINT, "R8_SINT", SLANG_SCALAR_TYPE_INT8, 1, 1);

        set(Format::R16G16B16A16_UNORM, "R16G16B16A16_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 4, 8);
        set(Format::R16G16_UNORM, "R16G16_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 2, 4);
        set(Format::R16_UNORM, "R16_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 1, 2);

        set(Format::R8G8B8A8_UNORM, "R8G8B8A8_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);
        set(Format::R8G8B8A8_UNORM_SRGB, "R8G8B8A8_UNORM_SRGB", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);
        set(Format::R8G8_UNORM, "R8G8_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 2, 2);
        set(Format::R8_UNORM, "R8_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 1, 1);
        set(Format::B8G8R8A8_UNORM, "B8G8R8A8_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);
        set(Format::B8G8R8A8_UNORM_SRGB, "B8G8R8A8_UNORM_SRGB", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);
        set(Format::B8G8R8X8_UNORM, "B8G8R8X8_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);
        set(Format::B8G8R8X8_UNORM_SRGB, "B8G8R8X8_UNORM_SRGB", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);

        set(Format::R16G16B16A16_SNORM, "R16G16B16A16_SNORM", SLANG_SCALAR_TYPE_FLOAT32, 4, 8);
        set(Format::R16G16_SNORM, "R16G16_SNORM", SLANG_SCALAR_TYPE_FLOAT32, 2, 4);
        set(Format::R16_SNORM, "R16_SNORM", SLANG_SCALAR_TYPE_FLOAT32, 1, 2);

        set(Format::R8G8B8A8_SNORM, "R8G8B8A8_SNORM", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);
        set(Format::R8G8_SNORM, "R8G8_SNORM", SLANG_SCALAR_TYPE_FLOAT32, 2, 2);
        set(Format::R8_SNORM, "R8_SNORM", SLANG_SCALAR_TYPE_FLOAT32, 1, 1);

        set(Format::D32_FLOAT, "D32_FLOAT", SLANG_SCALAR_TYPE_FLOAT32, 1, 4);
        set(Format::D16_UNORM, "D16_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 1, 2);
        set(Format::D32_FLOAT_S8_UINT, "D32_FLOAT_S8_UINT", SLANG_SCALAR_TYPE_FLOAT32, 2, 8);
        set(Format::R32_FLOAT_X32_TYPELESS, "R32_FLOAT_X32_TYPELESS", SLANG_SCALAR_TYPE_FLOAT32, 2, 8);

        set(Format::B4G4R4A4_UNORM, "B4G4R4A4_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 4, 2);
        set(Format::B5G6R5_UNORM, "B5G6R5_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 3, 2);
        set(Format::B5G5R5A1_UNORM, "B5G5R5A1_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 4, 2);

        set(Format::R9G9B9E5_SHAREDEXP, "R9G9B9E5_SHAREDEXP", SLANG_SCALAR_TYPE_FLOAT32, 3, 4);
        set(Format::R10G10B10A2_TYPELESS, "R10G10B10A2_TYPELESS", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);
        set(Format::R10G10B10A2_UNORM, "R10G10B10A2_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 4, 4);
        set(Format::R10G10B10A2_UINT, "R10G10B10A2_UINT", SLANG_SCALAR_TYPE_UINT32, 4, 4);
        set(Format::R11G11B10_FLOAT, "R11G11B10_FLOAT", SLANG_SCALAR_TYPE_FLOAT32, 3, 4);

        set(Format::BC1_UNORM, "BC1_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 4, 8, 16, 4, 4);
        set(Format::BC1_UNORM_SRGB, "BC1_UNORM_SRGB", SLANG_SCALAR_TYPE_FLOAT32, 4, 8, 16, 4, 4);
        set(Format::BC2_UNORM, "BC2_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 4, 16, 16, 4, 4);
        set(Format::BC2_UNORM_SRGB, "BC2_UNORM_SRGB", SLANG_SCALAR_TYPE_FLOAT32, 4, 16, 16, 4, 4);
        set(Format::BC3_UNORM, "BC3_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 4, 16, 16, 4, 4);
        set(Format::BC3_UNORM_SRGB, "BC3_UNORM_SRGB", SLANG_SCALAR_TYPE_FLOAT32, 4, 16, 16, 4, 4);
        set(Format::BC4_UNORM, "BC4_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 1, 8, 16, 4, 4);
        set(Format::BC4_SNORM, "BC4_SNORM", SLANG_SCALAR_TYPE_FLOAT32, 1, 8, 16, 4, 4);
        set(Format::BC5_UNORM, "BC5_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 2, 16, 16, 4, 4);
        set(Format::BC5_SNORM, "BC5_SNORM", SLANG_SCALAR_TYPE_FLOAT32, 2, 16, 16, 4, 4);
        set(Format::BC6H_UF16, "BC6H_UF16", SLANG_SCALAR_TYPE_FLOAT32, 3, 16, 16, 4, 4);
        set(Format::BC6H_SF16, "BC6H_SF16", SLANG_SCALAR_TYPE_FLOAT32, 3, 16, 16, 4, 4);
        set(Format::BC7_UNORM, "BC7_UNORM", SLANG_SCALAR_TYPE_FLOAT32, 4, 16, 16, 4, 4);
        set(Format::BC7_UNORM_SRGB, "BC7_UNORM_SRGB", SLANG_SCALAR_TYPE_FLOAT32, 4, 16, 16, 4, 4);
    }

    void set(
        Format format,
        const char* name,
        SlangScalarType type,
        Index channelCount,
        uint32_t blockSizeInBytes,
        uint32_t pixelsPerBlock = 1,
        uint32_t blockWidth = 1,
        uint32_t blockHeight = 1
    )
    {
        FormatInfo& info = m_infos[Index(format)];
        info.name = name;
        info.channelCount = uint8_t(channelCount);
        info.channelType = uint8_t(type);

        info.blockSizeInBytes = blockSizeInBytes;
        info.pixelsPerBlock = pixelsPerBlock;
        info.blockWidth = blockWidth;
        info.blockHeight = blockHeight;
    }

    const FormatInfo& get(Format format) const { return m_infos[Index(format)]; }

    FormatInfo m_infos[Index(Format::_Count)];
};

static const FormatInfoMap s_formatInfoMap;

extern "C"
{
    SLANG_RHI_API bool SLANG_MCALL rhiIsCompressedFormat(Format format)
    {
        switch (format)
        {
        case Format::BC1_UNORM:
        case Format::BC1_UNORM_SRGB:
        case Format::BC2_UNORM:
        case Format::BC2_UNORM_SRGB:
        case Format::BC3_UNORM:
        case Format::BC3_UNORM_SRGB:
        case Format::BC4_UNORM:
        case Format::BC4_SNORM:
        case Format::BC5_UNORM:
        case Format::BC5_SNORM:
        case Format::BC6H_UF16:
        case Format::BC6H_SF16:
        case Format::BC7_UNORM:
        case Format::BC7_UNORM_SRGB:
            return true;
        default:
            return false;
        }
    }

    SLANG_RHI_API bool SLANG_MCALL rhiIsTypelessFormat(Format format)
    {
        switch (format)
        {
        case Format::R32G32B32A32_TYPELESS:
        case Format::R32G32B32_TYPELESS:
        case Format::R32G32_TYPELESS:
        case Format::R32_TYPELESS:
        case Format::R16G16B16A16_TYPELESS:
        case Format::R16G16_TYPELESS:
        case Format::R16_TYPELESS:
        case Format::R8G8B8A8_TYPELESS:
        case Format::R8G8_TYPELESS:
        case Format::R8_TYPELESS:
        case Format::B8G8R8A8_TYPELESS:
        case Format::R10G10B10A2_TYPELESS:
            return true;
        default:
            return false;
        }
    }

    SLANG_RHI_API Result SLANG_MCALL rhiGetFormatInfo(Format format, FormatInfo* outInfo)
    {
        *outInfo = s_formatInfoMap.get(format);
        return SLANG_OK;
    }

    SLANG_RHI_API Result SLANG_MCALL rhiGetAdapters(DeviceType type, ISlangBlob** outAdaptersBlob)
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

    Result _createDevice(const IDevice::Desc* desc, IDevice** outDevice)
    {
        switch (desc->deviceType)
        {
        case DeviceType::Default:
        {
            IDevice::Desc newDesc = *desc;
            newDesc.deviceType = DeviceType::D3D12;
            if (_createDevice(&newDesc, outDevice) == SLANG_OK)
                return SLANG_OK;
            newDesc.deviceType = DeviceType::Vulkan;
            if (_createDevice(&newDesc, outDevice) == SLANG_OK)
                return SLANG_OK;
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
        case DeviceType::CPU:
        {
            return createCPUDevice(desc, outDevice);
        }
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

    SLANG_RHI_API Result SLANG_MCALL rhiCreateDevice(const IDevice::Desc* desc, IDevice** outDevice)
    {
        ComPtr<IDevice> innerDevice;
        auto resultCode = _createDevice(desc, innerDevice.writeRef());
        if (SLANG_FAILED(resultCode))
            return resultCode;
        if (!debugLayerEnabled)
        {
            returnComPtr(outDevice, innerDevice);
            return resultCode;
        }
        RefPtr<debug::DebugDevice> debugDevice = new debug::DebugDevice();
        debugDevice->baseObject = innerDevice;
        returnComPtr(outDevice, debugDevice);
        return resultCode;
    }

    SLANG_RHI_API Result SLANG_MCALL rhiReportLiveObjects()
    {
#if SLANG_RHI_ENABLE_D3D12
        SLANG_RETURN_ON_FAIL(reportD3DLiveObjects());
#endif
        return SLANG_OK;
    }

    SLANG_RHI_API Result SLANG_MCALL rhiSetDebugCallback(IDebugCallback* callback)
    {
        _getDebugCallback() = callback;
        return SLANG_OK;
    }

    SLANG_RHI_API void SLANG_MCALL rhiEnableDebugLayer()
    {
        debugLayerEnabled = true;
    }

    const char* SLANG_MCALL rhiGetDeviceTypeName(DeviceType type)
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
        default:
            return "?";
        }
    }

    bool rhiIsDeviceTypeSupported(DeviceType type)
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
            return true;
        case DeviceType::CUDA:
#if SLANG_RHI_ENABLE_CUDA
            return rhiCudaApiInit();
#else
            return false;
#endif
        case DeviceType::WGPU:
            return SLANG_RHI_ENABLE_WGPU;
        default:
            return false;
        }
    }
}

} // namespace rhi
