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

static const FormatInfo s_formatInfos[] = {
    // clang-format off
    // format                   name                kind                    cc  ct                          bs  ppb bw  bh  red    green  blue   alpha  depth  stenci signed srgb   comp
    { Format::Undefined,        "Undefined",        FormatKind::Integer,    0,  SLANG_SCALAR_TYPE_NONE,     0,  0,  0,  0,  false, false, false, false, false, false, false, false, false },
    { Format::R8Uint,           "R8Uint",           FormatKind::Integer,    1,  SLANG_SCALAR_TYPE_UINT8,    1,  1,  1,  1,  true,  false, false, false, false, false, false, false, false },
    { Format::R8Sint,           "R8Sint",           FormatKind::Integer,    1,  SLANG_SCALAR_TYPE_INT8,     1,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false },
    { Format::R8Unorm,          "R8Unorm",          FormatKind::Normalized, 1,  SLANG_SCALAR_TYPE_FLOAT32,  1,  1,  1,  1,  true,  false, false, false, false, false, false, false, false },
    { Format::R8Snorm,          "R8Snorm",          FormatKind::Normalized, 1,  SLANG_SCALAR_TYPE_FLOAT32,  1,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false },

    { Format::RG8Uint,          "RG8Uint",          FormatKind::Integer,    2,  SLANG_SCALAR_TYPE_UINT8,    2,  1,  1,  1,  true,  true,  false, false, false, false, false, false, false },
    { Format::RG8Sint,          "RG8Sint",          FormatKind::Integer,    2,  SLANG_SCALAR_TYPE_INT8,     2,  1,  1,  1,  true,  true,  false, false, false, false, true,  false, false },
    { Format::RG8Unorm,         "RG8Unorm",         FormatKind::Normalized, 2,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  true,  true,  false, false, false, false, false, false, false },
    { Format::RG8Snorm,         "RG8Snorm",         FormatKind::Normalized, 2,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  true,  true,  false, false, false, false, true,  false, false },

    { Format::RGBA8Uint,        "RGBA8Uint",        FormatKind::Integer,    4,  SLANG_SCALAR_TYPE_UINT8,    4,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false },
    { Format::RGBA8Sint,        "RGBA8Sint",        FormatKind::Integer,    4,  SLANG_SCALAR_TYPE_INT8,     4,  1,  1,  1,  true,  true,  true,  true,  false, false, true,  false, false },
    { Format::RGBA8Unorm,       "RGBA8Unorm",       FormatKind::Normalized, 4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false },
    { Format::RGBA8UnormSrgb,   "RGBA8UnormSrgb",   FormatKind::Normalized, 4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  true,  false, false, false, true,  false },
    { Format::RGBA8Snorm,       "RGBA8Snorm",       FormatKind::Normalized, 4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  true,  false, false, true,  false, false },

    { Format::BGRA8Unorm,       "BGRA8Unorm",       FormatKind::Normalized, 4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false },
    { Format::BGRA8UnormSrgb,   "BGRA8UnormSrgb",   FormatKind::Normalized, 4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  true,  false, false, false, true,  false },
    { Format::BGRX8Unorm,       "BGRX8Unorm",       FormatKind::Normalized, 4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  false, false, false, false, false, false },
    { Format::BGRX8UnormSrgb,   "BGRX8UnormSrgb",   FormatKind::Normalized, 4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  false, false, false, false, true,  false },

    { Format::R16Uint,          "R16Uint",          FormatKind::Integer,    1,  SLANG_SCALAR_TYPE_UINT16,   2,  1,  1,  1,  true,  false, false, false, false, false, false, false, false },
    { Format::R16Sint,          "R16Sint",          FormatKind::Integer,    1,  SLANG_SCALAR_TYPE_INT16,    2,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false },
    { Format::R16Unorm,         "R16Unorm",         FormatKind::Normalized, 1,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  true,  false, false, false, false, false, false, false, false },
    { Format::R16Snorm,         "R16Snorm",         FormatKind::Normalized, 1,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false },
    { Format::R16Float,         "R16Float",         FormatKind::Float,      1,  SLANG_SCALAR_TYPE_FLOAT16,  2,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false },

    { Format::RG16Uint,         "RG16Uint",         FormatKind::Integer,    2,  SLANG_SCALAR_TYPE_UINT16,   4,  1,  1,  1,  true,  true,  false, false, false, false, false, false, false },
    { Format::RG16Sint,         "RG16Sint",         FormatKind::Integer,    2,  SLANG_SCALAR_TYPE_INT16,    4,  1,  1,  1,  true,  true,  false, false, false, false, true,  false, false },
    { Format::RG16Unorm,        "RG16Unorm",        FormatKind::Normalized, 2,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  false, false, false, false, false, false, false },
    { Format::RG16Snorm,        "RG16Snorm",        FormatKind::Normalized, 2,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  false, false, false, false, true,  false, false },
    { Format::RG16Float,        "RG16Float",        FormatKind::Float,      2,  SLANG_SCALAR_TYPE_FLOAT16,  4,  1,  1,  1,  true,  true,  false, false, false, false, true,  false, false },

    { Format::RGBA16Uint,       "RGBA16Uint",       FormatKind::Integer,    4,  SLANG_SCALAR_TYPE_UINT16,   8,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false },
    { Format::RGBA16Sint,       "RGBA16Sint",       FormatKind::Integer,    4,  SLANG_SCALAR_TYPE_INT16,    8,  1,  1,  1,  true,  true,  true,  true,  false, false, true,  false, false },
    { Format::RGBA16Unorm,      "RGBA16Unorm",      FormatKind::Normalized, 4,  SLANG_SCALAR_TYPE_FLOAT32,  8,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false },
    { Format::RGBA16Snorm,      "RGBA16Snorm",      FormatKind::Normalized, 4,  SLANG_SCALAR_TYPE_FLOAT32,  8,  1,  1,  1,  true,  true,  true,  true,  false, false, true,  false, false },
    { Format::RGBA16Float,      "RGBA16Float",      FormatKind::Float,      4,  SLANG_SCALAR_TYPE_FLOAT16,  8,  1,  1,  1,  true,  true,  true,  true,  false, false, true,  false, false },

    { Format::R32Uint,          "R32Uint",          FormatKind::Integer,    1,  SLANG_SCALAR_TYPE_UINT32,   4,  1,  1,  1,  true,  false, false, false, false, false, false, false, false },
    { Format::R32Sint,          "R32Sint",          FormatKind::Integer,    1,  SLANG_SCALAR_TYPE_INT32,    4,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false },
    { Format::R32Float,         "R32Float",         FormatKind::Float,      1,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false },

    { Format::RG32Uint,         "RG32Uint",         FormatKind::Integer,    2,  SLANG_SCALAR_TYPE_UINT32,   8,  1,  1,  1,  true,  true,  false, false, false, false, false, false, false },
    { Format::RG32Sint,         "RG32Sint",         FormatKind::Integer,    2,  SLANG_SCALAR_TYPE_INT32,    8,  1,  1,  1,  true,  true,  false, false, false, false, true,  false, false },
    { Format::RG32Float,        "RG32Float",        FormatKind::Float,      2,  SLANG_SCALAR_TYPE_FLOAT32,  8,  1,  1,  1,  true,  true,  false, false, false, false, true,  false, false },

    { Format::RGB32Uint,        "RGB32Uint",        FormatKind::Integer,    3,  SLANG_SCALAR_TYPE_UINT32,   12, 1,  1,  1,  true,  true,  true,  false, false, false, false, false, false },
    { Format::RGB32Sint,        "RGB32Sint",        FormatKind::Integer,    3,  SLANG_SCALAR_TYPE_INT32,    12, 1,  1,  1,  true,  true,  true,  false, false, false, true,  false, false },
    { Format::RGB32Float,       "RGB32Float",       FormatKind::Float,      3,  SLANG_SCALAR_TYPE_FLOAT32,  12, 1,  1,  1,  true,  true,  true,  false, false, false, true,  false, false },

    { Format::RGBA32Uint,       "RGBA32Uint",       FormatKind::Integer,    4,  SLANG_SCALAR_TYPE_UINT32,   16, 1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false },
    { Format::RGBA32Sint,       "RGBA32Sint",       FormatKind::Integer,    4,  SLANG_SCALAR_TYPE_INT32,    16, 1,  1,  1,  true,  true,  true,  true,  false, false, true,  false, false },
    { Format::RGBA32Float,      "RGBA32Float",      FormatKind::Float,      4,  SLANG_SCALAR_TYPE_FLOAT32,  16, 1,  1,  1,  true,  true,  true,  true,  false, false, true,  false, false },

    { Format::R64Uint,          "R64Uint",          FormatKind::Integer,    1,  SLANG_SCALAR_TYPE_UINT64,   8,  1,  1,  1,  true,  false, false, false, false, false, false, false, false },
    { Format::R64Sint,          "R64Sint",          FormatKind::Integer,    1,  SLANG_SCALAR_TYPE_INT64,    8,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false },

    { Format::BGRA4Unorm,       "BGRA4Unorm",       FormatKind::Normalized, 4,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false },
    { Format::B5G6R5Unorm,      "B5G6R5Unorm",      FormatKind::Normalized, 3,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false },
    { Format::BGR5A1Unorm,      "BGR5A1Unorm",      FormatKind::Normalized, 4,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false },

    { Format::RGB9E5Ufloat,     "RGB9E5Ufloat",     FormatKind::Float,      3,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  false, false, false, false, false, false },
    { Format::RGB10A2Uint,      "RGB10A2Uint",      FormatKind::Integer,    4,  SLANG_SCALAR_TYPE_UINT32,   4,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false },
    { Format::RGB10A2Unorm,     "RGB10A2Unorm",     FormatKind::Normalized, 4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false },
    { Format::R11G11B10Float,   "R11G11B10Float",   FormatKind::Float,      3,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  false, false, false, true,  false, false },

    { Format::D32Float,         "D32Float",         FormatKind::DepthStencil,1, SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  false, false, false, false, true,  false, true,  false, false },
    { Format::D16Unorm,         "D16Unorm",         FormatKind::DepthStencil,1, SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  false, false, false, false, true,  false, false, false, false },
    { Format::D32FloatS8Uint,   "D32FloatS8Uint",   FormatKind::DepthStencil,2, SLANG_SCALAR_TYPE_FLOAT32,  8,  1,  1,  1,  false, false, false, false, true,  true,  false, false, false },

    { Format::BC1Unorm,         "BC1Unorm",         FormatKind::Normalized, 4, SLANG_SCALAR_TYPE_FLOAT32,   8,  16, 4,  4,  true,  true,  true,  true,  false, false, false, false, true  },
    { Format::BC1UnormSrgb,     "BC1UnormSrgb",     FormatKind::Normalized, 4, SLANG_SCALAR_TYPE_FLOAT32,   8,  16, 4,  4,  true,  true,  true,  true,  false, false, false, true,  true  },
    { Format::BC2Unorm,         "BC2Unorm",         FormatKind::Normalized, 4, SLANG_SCALAR_TYPE_FLOAT32,   16, 16, 4,  4,  true,  true,  true,  true,  false, false, false, false, true  },
    { Format::BC2UnormSrgb,     "BC2UnormSrgb",     FormatKind::Normalized, 4, SLANG_SCALAR_TYPE_FLOAT32,   16, 16, 4,  4,  true,  true,  true,  true,  false, false, false, true,  true  },
    { Format::BC3Unorm,         "BC3Unorm",         FormatKind::Normalized, 4, SLANG_SCALAR_TYPE_FLOAT32,   16, 16, 4,  4,  true,  true,  true,  true,  false, false, false, false, true  },
    { Format::BC3UnormSrgb,     "BC3UnormSrgb",     FormatKind::Normalized, 4, SLANG_SCALAR_TYPE_FLOAT32,   16, 16, 4,  4,  true,  true,  true,  true,  false, false, false, true,  true  },
    { Format::BC4Unorm,         "BC4Unorm",         FormatKind::Normalized, 1, SLANG_SCALAR_TYPE_FLOAT32,   8,  16, 4,  4,  true,  false, false, false, false, false, false, false, true  },
    { Format::BC4Snorm,         "BC4Snorm",         FormatKind::Normalized, 1, SLANG_SCALAR_TYPE_FLOAT32,   8,  16, 4,  4,  true,  false, false, false, false, false, true,  false, true  },
    { Format::BC5Unorm,         "BC5Unorm",         FormatKind::Normalized, 2, SLANG_SCALAR_TYPE_FLOAT32,   16, 16, 4,  4,  true,  true,  false, false, false, false, false, false, true  },
    { Format::BC5Snorm,         "BC5Snorm",         FormatKind::Normalized, 2, SLANG_SCALAR_TYPE_FLOAT32,   16, 16, 4,  4,  true,  true,  false, false, false, false, false, true,  true  },
    { Format::BC6HUfloat,       "BC6HUfloat",       FormatKind::Float,      3, SLANG_SCALAR_TYPE_FLOAT32,   16, 16, 4,  4,  true,  true,  true,  false, false, false, false, false, true  },
    { Format::BC6HSfloat,       "BC6HSfloat",       FormatKind::Float,      3, SLANG_SCALAR_TYPE_FLOAT32,   16, 16, 4,  4,  true,  true,  true,  false, false, false, false, true,  true  },
    { Format::BC7Unorm,         "BC7Unorm",         FormatKind::Normalized, 4, SLANG_SCALAR_TYPE_FLOAT32,   16, 16, 4,  4,  true,  true,  true,  true,  false, false, false, false, true  },
    { Format::BC7UnormSrgb,     "BC7UnormSrgb",     FormatKind::Normalized, 4, SLANG_SCALAR_TYPE_FLOAT32,   16, 16, 4,  4,  true,  true,  true,  true,  false, false, false, true,  true  },
    // clang-format on
};

static_assert(SLANG_COUNT_OF(s_formatInfos) == size_t(Format::_Count), "Format table count mismatch");

inline const FormatInfo& _getFormatInfo(Format format)
{
    SLANG_RHI_ASSERT(size_t(format) < size_t(Format::_Count));
    SLANG_RHI_ASSERT(s_formatInfos[size_t(format)].format == format);
    return s_formatInfos[size_t(format)];
}

class RHI : public IRHI
{
public:
    bool debugLayersEnabled = false;

    virtual const FormatInfo& getFormatInfo(Format format) override { return _getFormatInfo(format); }
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
