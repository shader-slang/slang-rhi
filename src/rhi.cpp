#include <slang-rhi.h>

#include "debug-layer/debug-device.h"
#include "rhi-shared.h"

#include "core/common.h"
#include "core/task-pool.h"

#include <cstring>
#include <vector>

namespace rhi {

IAdapter* getD3D11Adapter(uint32_t index);
IAdapter* getD3D12Adapter(uint32_t index);
IAdapter* getVKAdapter(uint32_t index);
IAdapter* getMetalAdapter(uint32_t index);
IAdapter* getCUDAAdapter(uint32_t index);
IAdapter* getCPUAdapter(uint32_t index);
IAdapter* getWGPUAdapter(uint32_t index);

Result createD3D11Device(const DeviceDesc* desc, IDevice** outDevice);
Result createD3D12Device(const DeviceDesc* desc, IDevice** outDevice);
Result createVKDevice(const DeviceDesc* desc, IDevice** outDevice);
Result createMetalDevice(const DeviceDesc* desc, IDevice** outDevice);
Result createCUDADevice(const DeviceDesc* desc, IDevice** outDevice);
Result createCPUDevice(const DeviceDesc* desc, IDevice** outDevice);
Result createWGPUDevice(const DeviceDesc* desc, IDevice** outDevice);

Result reportD3DLiveObjects();
void enableD3D12DebugLayerIfAvailable();

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Global Functions !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */

static const FormatInfo s_formatInfos[] = {
    // clang-format off
    // format                   name                slangName           kind                        cc  ct                          bs  ppb bw  bh  red    green  blue   alpha  depth  stenci signed srgb   comp   nonpo2
    { Format::Undefined,        "Undefined",        nullptr,            FormatKind::Integer,        0,  SLANG_SCALAR_TYPE_NONE,     0,  0,  0,  0,  false, false, false, false, false, false, false, false, false, true  },
    { Format::R8Uint,           "R8Uint",           "r8ui",             FormatKind::Integer,        1,  SLANG_SCALAR_TYPE_UINT8,    1,  1,  1,  1,  true,  false, false, false, false, false, false, false, false, true  },
    { Format::R8Sint,           "R8Sint",           "r8i",              FormatKind::Integer,        1,  SLANG_SCALAR_TYPE_INT8,     1,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false, true  },
    { Format::R8Unorm,          "R8Unorm",          "r8",               FormatKind::Normalized,     1,  SLANG_SCALAR_TYPE_FLOAT32,  1,  1,  1,  1,  true,  false, false, false, false, false, false, false, false, true  },
    { Format::R8Snorm,          "R8Snorm",          "r8snorm",          FormatKind::Normalized,     1,  SLANG_SCALAR_TYPE_FLOAT32,  1,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false, true  },

    { Format::RG8Uint,          "RG8Uint",          "rg8ui",            FormatKind::Integer,        2,  SLANG_SCALAR_TYPE_UINT8,    2,  1,  1,  1,  true,  true,  false, false, false, false, false, false, false, true  },
    { Format::RG8Sint,          "RG8Sint",          "rg8i",             FormatKind::Integer,        2,  SLANG_SCALAR_TYPE_INT8,     2,  1,  1,  1,  true,  true,  false, false, false, false, true,  false, false, true  },
    { Format::RG8Unorm,         "RG8Unorm",         "rg8",              FormatKind::Normalized,     2,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  true,  true,  false, false, false, false, false, false, false, true  },
    { Format::RG8Snorm,         "RG8Snorm",         "rg8snorm",         FormatKind::Normalized,     2,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  true,  true,  false, false, false, false, true,  false, false, true  },

    { Format::RGBA8Uint,        "RGBA8Uint",        "rgba8ui",          FormatKind::Integer,        4,  SLANG_SCALAR_TYPE_UINT8,    4,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false, true  },
    { Format::RGBA8Sint,        "RGBA8Sint",        "rgba8i",           FormatKind::Integer,        4,  SLANG_SCALAR_TYPE_INT8,     4,  1,  1,  1,  true,  true,  true,  true,  false, false, true,  false, false, true  },
    { Format::RGBA8Unorm,       "RGBA8Unorm",       "rgba8",            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false, true  },
    { Format::RGBA8UnormSrgb,   "RGBA8UnormSrgb",   nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  true,  false, false, false, true,  false, true  },
    { Format::RGBA8Snorm,       "RGBA8Snorm",       "rgba8snorm",       FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  true,  false, false, true,  false, false, true  },

    { Format::BGRA8Unorm,       "BGRA8Unorm",       nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false, true  },
    { Format::BGRA8UnormSrgb,   "BGRA8UnormSrgb",   nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  true,  false, false, false, true,  false, true  },
    { Format::BGRX8Unorm,       "BGRX8Unorm",       nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  false, false, false, false, false, false, true  },
    { Format::BGRX8UnormSrgb,   "BGRX8UnormSrgb",   nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  false, false, false, false, true,  false, true  },

    { Format::R16Uint,          "R16Uint",          "r16ui",            FormatKind::Integer,        1,  SLANG_SCALAR_TYPE_UINT16,   2,  1,  1,  1,  true,  false, false, false, false, false, false, false, false, true  },
    { Format::R16Sint,          "R16Sint",          "r16i",             FormatKind::Integer,        1,  SLANG_SCALAR_TYPE_INT16,    2,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false, true  },
    { Format::R16Unorm,         "R16Unorm",         "r16",              FormatKind::Normalized,     1,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  true,  false, false, false, false, false, false, false, false, true  },
    { Format::R16Snorm,         "R16Snorm",         "r16snorm",         FormatKind::Normalized,     1,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false, true  },
    { Format::R16Float,         "R16Float",         "r16f",             FormatKind::Float,          1,  SLANG_SCALAR_TYPE_FLOAT16,  2,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false, true  },

    { Format::RG16Uint,         "RG16Uint",         "rg16ui",           FormatKind::Integer,        2,  SLANG_SCALAR_TYPE_UINT16,   4,  1,  1,  1,  true,  true,  false, false, false, false, false, false, false, true  },
    { Format::RG16Sint,         "RG16Sint",         "rg16i",            FormatKind::Integer,        2,  SLANG_SCALAR_TYPE_INT16,    4,  1,  1,  1,  true,  true,  false, false, false, false, true,  false, false, true  },
    { Format::RG16Unorm,        "RG16Unorm",        "rg16",             FormatKind::Normalized,     2,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  false, false, false, false, false, false, false, true  },
    { Format::RG16Snorm,        "RG16Snorm",        "rg16snorm",        FormatKind::Normalized,     2,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  false, false, false, false, true,  false, false, true  },
    { Format::RG16Float,        "RG16Float",        "rg16f",            FormatKind::Float,          2,  SLANG_SCALAR_TYPE_FLOAT16,  4,  1,  1,  1,  true,  true,  false, false, false, false, true,  false, false, true  },

    { Format::RGBA16Uint,       "RGBA16Uint",       "rgba16ui",         FormatKind::Integer,        4,  SLANG_SCALAR_TYPE_UINT16,   8,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false, true  },
    { Format::RGBA16Sint,       "RGBA16Sint",       "rgba16i",          FormatKind::Integer,        4,  SLANG_SCALAR_TYPE_INT16,    8,  1,  1,  1,  true,  true,  true,  true,  false, false, true,  false, false, true  },
    { Format::RGBA16Unorm,      "RGBA16Unorm",      "rgba16",           FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  8,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false, true  },
    { Format::RGBA16Snorm,      "RGBA16Snorm",      "rgba16snorm",      FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  8,  1,  1,  1,  true,  true,  true,  true,  false, false, true,  false, false, true  },
    { Format::RGBA16Float,      "RGBA16Float",      "rgba16f",          FormatKind::Float,          4,  SLANG_SCALAR_TYPE_FLOAT16,  8,  1,  1,  1,  true,  true,  true,  true,  false, false, true,  false, false, true  },

    { Format::R32Uint,          "R32Uint",          "r32ui",            FormatKind::Integer,        1,  SLANG_SCALAR_TYPE_UINT32,   4,  1,  1,  1,  true,  false, false, false, false, false, false, false, false, true  },
    { Format::R32Sint,          "R32Sint",          "r32i",             FormatKind::Integer,        1,  SLANG_SCALAR_TYPE_INT32,    4,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false, true  },
    { Format::R32Float,         "R32Float",         "r32f",             FormatKind::Float,          1,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false, true  },

    { Format::RG32Uint,         "RG32Uint",         "rg32ui",           FormatKind::Integer,        2,  SLANG_SCALAR_TYPE_UINT32,   8,  1,  1,  1,  true,  true,  false, false, false, false, false, false, false, true  },
    { Format::RG32Sint,         "RG32Sint",         "rg32i",            FormatKind::Integer,        2,  SLANG_SCALAR_TYPE_INT32,    8,  1,  1,  1,  true,  true,  false, false, false, false, true,  false, false, true  },
    { Format::RG32Float,        "RG32Float",        "rg32f",            FormatKind::Float,          2,  SLANG_SCALAR_TYPE_FLOAT32,  8,  1,  1,  1,  true,  true,  false, false, false, false, true,  false, false, true  },

    { Format::RGB32Uint,        "RGB32Uint",        nullptr,            FormatKind::Integer,        3,  SLANG_SCALAR_TYPE_UINT32,   12, 1,  1,  1,  true,  true,  true,  false, false, false, false, false, false, true  },
    { Format::RGB32Sint,        "RGB32Sint",        nullptr,            FormatKind::Integer,        3,  SLANG_SCALAR_TYPE_INT32,    12, 1,  1,  1,  true,  true,  true,  false, false, false, true,  false, false, true  },
    { Format::RGB32Float,       "RGB32Float",       nullptr,            FormatKind::Float,          3,  SLANG_SCALAR_TYPE_FLOAT32,  12, 1,  1,  1,  true,  true,  true,  false, false, false, true,  false, false, true  },

    { Format::RGBA32Uint,       "RGBA32Uint",       "rgba32ui",         FormatKind::Integer,        4,  SLANG_SCALAR_TYPE_UINT32,   16, 1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false, true  },
    { Format::RGBA32Sint,       "RGBA32Sint",       "rgba32i",          FormatKind::Integer,        4,  SLANG_SCALAR_TYPE_INT32,    16, 1,  1,  1,  true,  true,  true,  true,  false, false, true,  false, false, true  },
    { Format::RGBA32Float,      "RGBA32Float",      "rgba32f",          FormatKind::Float,          4,  SLANG_SCALAR_TYPE_FLOAT32,  16, 1,  1,  1,  true,  true,  true,  true,  false, false, true,  false, false, true  },

    { Format::R64Uint,          "R64Uint",          "r64ui",            FormatKind::Integer,        1,  SLANG_SCALAR_TYPE_UINT64,   8,  1,  1,  1,  true,  false, false, false, false, false, false, false, false, true  },
    { Format::R64Sint,          "R64Sint",          "r64i",             FormatKind::Integer,        1,  SLANG_SCALAR_TYPE_INT64,    8,  1,  1,  1,  true,  false, false, false, false, false, true,  false, false, true  },

    { Format::BGRA4Unorm,       "BGRA4Unorm",       nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false, true  },
    { Format::B5G6R5Unorm,      "B5G6R5Unorm",      nullptr,            FormatKind::Normalized,     3,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false, true  },
    { Format::BGR5A1Unorm,      "BGR5A1Unorm",      nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false, true  },

    { Format::RGB9E5Ufloat,     "RGB9E5Ufloat",     nullptr,            FormatKind::Float,          3,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  false, false, false, false, false, false, true  },
    { Format::RGB10A2Uint,      "RGB10A2Uint",      "rgb10_a2ui",       FormatKind::Integer,        4,  SLANG_SCALAR_TYPE_UINT32,   4,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false, true  },
    { Format::RGB10A2Unorm,     "RGB10A2Unorm",     "rgb10_a2",         FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  true,  false, false, false, false, false, true  },
    { Format::R11G11B10Float,   "R11G11B10Float",   "r11f_g11f_b10f",   FormatKind::Float,          3,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  true,  true,  true,  false, false, false, true,  false, false, true  },

    { Format::D32Float,         "D32Float",         nullptr,            FormatKind::DepthStencil,   1,  SLANG_SCALAR_TYPE_FLOAT32,  4,  1,  1,  1,  false, false, false, false, true,  false, true,  false, false, false },
    { Format::D16Unorm,         "D16Unorm",         nullptr,            FormatKind::DepthStencil,   1,  SLANG_SCALAR_TYPE_FLOAT32,  2,  1,  1,  1,  false, false, false, false, true,  false, false, false, false, false },
    { Format::D32FloatS8Uint,   "D32FloatS8Uint",   nullptr,            FormatKind::DepthStencil,   2,  SLANG_SCALAR_TYPE_FLOAT32,  8,  1,  1,  1,  false, false, false, false, true,  true,  false, false, false, false },

    { Format::BC1Unorm,         "BC1Unorm",         nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  8,  16, 4,  4,  true,  true,  true,  true,  false, false, false, false, true,  true  },
    { Format::BC1UnormSrgb,     "BC1UnormSrgb",     nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  8,  16, 4,  4,  true,  true,  true,  true,  false, false, false, true,  true,  true  },
    { Format::BC2Unorm,         "BC2Unorm",         nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  16, 16, 4,  4,  true,  true,  true,  true,  false, false, false, false, true,  true  },
    { Format::BC2UnormSrgb,     "BC2UnormSrgb",     nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  16, 16, 4,  4,  true,  true,  true,  true,  false, false, false, true,  true,  true  },
    { Format::BC3Unorm,         "BC3Unorm",         nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  16, 16, 4,  4,  true,  true,  true,  true,  false, false, false, false, true,  true  },
    { Format::BC3UnormSrgb,     "BC3UnormSrgb",     nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  16, 16, 4,  4,  true,  true,  true,  true,  false, false, false, true,  true,  true  },
    { Format::BC4Unorm,         "BC4Unorm",         nullptr,            FormatKind::Normalized,     1,  SLANG_SCALAR_TYPE_FLOAT32,  8,  16, 4,  4,  true,  false, false, false, false, false, false, false, true,  true  },
    { Format::BC4Snorm,         "BC4Snorm",         nullptr,            FormatKind::Normalized,     1,  SLANG_SCALAR_TYPE_FLOAT32,  8,  16, 4,  4,  true,  false, false, false, false, false, true,  false, true,  true  },
    { Format::BC5Unorm,         "BC5Unorm",         nullptr,            FormatKind::Normalized,     2,  SLANG_SCALAR_TYPE_FLOAT32,  16, 16, 4,  4,  true,  true,  false, false, false, false, false, false, true,  true  },
    { Format::BC5Snorm,         "BC5Snorm",         nullptr,            FormatKind::Normalized,     2,  SLANG_SCALAR_TYPE_FLOAT32,  16, 16, 4,  4,  true,  true,  false, false, false, false, false, true,  true,  true  },
    { Format::BC6HUfloat,       "BC6HUfloat",       nullptr,            FormatKind::Float,          3,  SLANG_SCALAR_TYPE_FLOAT32,  16, 16, 4,  4,  true,  true,  true,  false, false, false, false, false, true,  true  },
    { Format::BC6HSfloat,       "BC6HSfloat",       nullptr,            FormatKind::Float,          3,  SLANG_SCALAR_TYPE_FLOAT32,  16, 16, 4,  4,  true,  true,  true,  false, false, false, false, true,  true,  true  },
    { Format::BC7Unorm,         "BC7Unorm",         nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  16, 16, 4,  4,  true,  true,  true,  true,  false, false, false, false, true,  true  },
    { Format::BC7UnormSrgb,     "BC7UnormSrgb",     nullptr,            FormatKind::Normalized,     4,  SLANG_SCALAR_TYPE_FLOAT32,  16, 16, 4,  4,  true,  true,  true,  true,  false, false, false, true,  true,  true  },
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
    virtual const char* getFeatureName(Feature feature) override;
    virtual const char* getCapabilityName(Capability capability) override;

    virtual IAdapter* getAdapter(DeviceType type, uint32_t index) override;
    virtual Result getAdapters(DeviceType type, ISlangBlob** outAdaptersBlob) override;
    virtual void enableDebugLayers() override;
    virtual Result createDevice(const DeviceDesc& desc, IDevice** outDevice) override;

    virtual Result createBlob(const void* data, size_t size, ISlangBlob** outBlob) override;

    virtual Result reportLiveObjects() override;
    virtual Result setTaskPool(ITaskPool* scheduler) override;

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

const char* RHI::getFeatureName(Feature feature)
{
#define SLANG_RHI_FEATURES_X(id, name) name,
    static const std::array<const char*, size_t(Feature::_Count)> kFeatureNames = {
        SLANG_RHI_FEATURES(SLANG_RHI_FEATURES_X)
    };
#undef SLANG_RHI_FEATURES_X
    return size_t(feature) < kFeatureNames.size() ? kFeatureNames[size_t(feature)] : nullptr;
}

const char* RHI::getCapabilityName(Capability capability)
{
#define SLANG_RHI_CAPABILITIES_X(x) #x,
    static const std::array<const char*, size_t(Capability::_Count)> kCapabilityNames = {
        SLANG_RHI_CAPABILITIES(SLANG_RHI_CAPABILITIES_X)
    };
#undef SLANG_RHI_CAPABILITIES_X
    return size_t(capability) < kCapabilityNames.size() ? kCapabilityNames[size_t(capability)] : nullptr;
}

IAdapter* RHI::getAdapter(DeviceType type, uint32_t index)
{
    switch (type)
    {
#if SLANG_RHI_ENABLE_D3D11
    case DeviceType::D3D11:
        return getD3D11Adapter(index);
#endif
#if SLANG_RHI_ENABLE_D3D12
    case DeviceType::D3D12:
        return getD3D12Adapter(index);
#endif
#if SLANG_RHI_ENABLE_VULKAN
    case DeviceType::Vulkan:
        return getVKAdapter(index);
#endif
#if SLANG_RHI_ENABLE_METAL
    case DeviceType::Metal:
        return getMetalAdapter(index);
#endif
#if SLANG_RHI_ENABLE_CUDA
    case DeviceType::CUDA:
        return getCUDAAdapter(index);
#endif
#if SLANG_RHI_ENABLE_CPU
    case DeviceType::CPU:
        return getCPUAdapter(index);
#endif
#if SLANG_RHI_ENABLE_WGPU
    case DeviceType::WGPU:
        return getWGPUAdapter(index);
#endif
    default:
        return nullptr;
    }
}

Result RHI::getAdapters(DeviceType type, ISlangBlob** outAdaptersBlob)
{
    std::vector<AdapterInfo> adapterInfos;

    for (uint32_t i = 0;; ++i)
    {
        IAdapter* adapter = getAdapter(type, i);
        if (!adapter)
        {
            break;
        }
        adapterInfos.push_back(adapter->getInfo());
    }

    auto adaptersBlob = OwnedBlob::create(adapterInfos.data(), adapterInfos.size() * sizeof(AdapterInfo));
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
        if (SLANG_SUCCEEDED(_createDevice(&newDesc, outDevice)))
            return SLANG_OK;
        newDesc.deviceType = DeviceType::Vulkan;
        if (SLANG_SUCCEEDED(_createDevice(&newDesc, outDevice)))
            return SLANG_OK;
#elif SLANG_LINUX_FAMILY
        newDesc.deviceType = DeviceType::Vulkan;
        if (SLANG_SUCCEEDED(_createDevice(&newDesc, outDevice)))
            return SLANG_OK;
#elif SLANG_APPLE_FAMILY
        newDesc.deviceType = DeviceType::Metal;
        if (SLANG_SUCCEEDED(_createDevice(&newDesc, outDevice)))
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
    RefPtr<debug::DebugDevice> debugDevice = new debug::DebugDevice(innerDevice->getInfo().deviceType, debugCallback);
    debugDevice->baseObject = innerDevice;
    returnComPtr(outDevice, debugDevice);
    return resultCode;
}

Result RHI::createBlob(const void* data, size_t size, ISlangBlob** outBlob)
{
    ComPtr<ISlangBlob> blob;
    if (data)
    {
        blob = OwnedBlob::create(data, size);
    }
    else
    {
        blob = OwnedBlob::create(size);
    }
    if (!blob)
    {
        return SLANG_FAIL;
    }
    returnComPtr(outBlob, blob);
    return SLANG_OK;
}

Result RHI::reportLiveObjects()
{
#if SLANG_RHI_ENABLE_REF_OBJECT_TRACKING
    RefObjectTracker::instance().reportLiveObjects();
#endif
#if SLANG_RHI_ENABLE_D3D11 | SLANG_RHI_ENABLE_D3D12
    SLANG_RETURN_ON_FAIL(reportD3DLiveObjects());
#endif
    return SLANG_OK;
}

Result RHI::setTaskPool(ITaskPool* taskPool)
{
    return setGlobalTaskPool(taskPool);
}

bool isDebugLayersEnabled()
{
    return RHI::getInstance()->debugLayersEnabled;
}

} // namespace rhi

extern "C" SLANG_RHI_API rhi::IRHI* SLANG_STDCALL rhiGetInstance()
{
    return rhi::RHI::getInstance();
}
