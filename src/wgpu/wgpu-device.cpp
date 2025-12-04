#include "wgpu-device.h"
#include "wgpu-buffer.h"
#include "wgpu-texture.h"
#include "wgpu-shader-program.h"
#include "wgpu-shader-object.h"
#include "wgpu-shader-object-layout.h"
#include "wgpu-utils.h"

#include "core/common.h"
#include "core/deferred.h"

#include <cstdio>
#include <vector>

namespace rhi::wgpu {

static inline WGPUDawnTogglesDescriptor getDawnTogglesDescriptor()
{
    // Currently no toggles are needed.
    static const std::vector<const char*> enabledToggles = {};
    static const std::vector<const char*> disabledToggles = {};
    WGPUDawnTogglesDescriptor togglesDesc = {};
    togglesDesc.chain.sType = WGPUSType_DawnTogglesDescriptor;
    togglesDesc.enabledToggleCount = enabledToggles.size();
    togglesDesc.enabledToggles = enabledToggles.data();
    togglesDesc.disabledToggleCount = disabledToggles.size();
    togglesDesc.disabledToggles = disabledToggles.data();
    return togglesDesc;
}

static inline Result createWGPUInstance(API& api, WGPUInstance* outInstance)
{
    WGPUInstanceDescriptor instanceDesc = {};
    instanceDesc.capabilities.timedWaitAnyEnable = WGPUBool(true);
    WGPUDawnTogglesDescriptor togglesDesc = getDawnTogglesDescriptor();
    instanceDesc.nextInChain = &togglesDesc.chain;
    WGPUInstance instance = api.wgpuCreateInstance(&instanceDesc);
    if (!instance)
    {
        return SLANG_FAIL;
    }
    *outInstance = instance;
    return SLANG_OK;
}

#if 0


#endif

static inline Result createWGPUAdapter(API& api, WGPUInstance instance, WGPUAdapter* outAdapter)
{
    // Request adapter.
    WGPURequestAdapterOptions options = {};
    options.powerPreference = WGPUPowerPreference_HighPerformance;
#if SLANG_WINDOWS_FAMILY
    // TODO: D3D12 Validation errors prevents use of D3D12, use Vulkan for now.
    options.backendType = WGPUBackendType_Vulkan;
#elif SLANG_LINUX_FAMILY
    options.backendType = WGPUBackendType_Vulkan;
#endif
    WGPUDawnTogglesDescriptor togglesDesc = getDawnTogglesDescriptor();
    options.nextInChain = &togglesDesc.chain;

    WGPUAdapter adapter = {};
    {
        WGPURequestAdapterStatus status = WGPURequestAdapterStatus(0);
        WGPURequestAdapterCallbackInfo callbackInfo = {};
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WGPURequestAdapterStatus status_,
                                   WGPUAdapter adapter_,
                                   WGPUStringView message,
                                   void* userdata1,
                                   void* userdata2)
        {
            *(WGPURequestAdapterStatus*)userdata1 = status_;
            *(WGPUAdapter*)userdata2 = adapter_;
        };
        callbackInfo.userdata1 = &status;
        callbackInfo.userdata2 = &adapter;
        WGPUFuture future = api.wgpuInstanceRequestAdapter(instance, &options, callbackInfo);
        WGPUFutureWaitInfo futures[1] = {{future}};
        uint64_t timeoutNS = UINT64_MAX;
        WGPUWaitStatus waitStatus = api.wgpuInstanceWaitAny(instance, SLANG_COUNT_OF(futures), futures, timeoutNS);
        if (waitStatus != WGPUWaitStatus_Success || status != WGPURequestAdapterStatus_Success)
        {
            return SLANG_FAIL;
        }
    }
    if (!adapter)
    {
        return SLANG_FAIL;
    }
    *outAdapter = adapter;
    return SLANG_OK;
}

Context::~Context()
{
    if (device)
    {
        api.wgpuDeviceRelease(device);
    }
    if (adapter)
    {
        api.wgpuAdapterRelease(adapter);
    }
    if (instance)
    {
        api.wgpuInstanceRelease(instance);
    }
}

DeviceImpl::~DeviceImpl()
{
    m_shaderObjectLayoutCache = decltype(m_shaderObjectLayoutCache)();

    m_shaderCache.free();
    m_uploadHeap.release();
    m_readbackHeap.release();

    m_queue.setNull();
}

Result DeviceImpl::getNativeDeviceHandles(DeviceNativeHandles* outHandles)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

void DeviceImpl::reportError(const char* func, WGPUStringView message)
{
    std::string msg = "WGPU error in " + std::string(func) + ": " + std::string(message.data, message.length);
    m_debugCallback->handleMessage(DebugMessageType::Error, DebugMessageSource::Driver, msg.c_str());
}

void DeviceImpl::reportDeviceLost(WGPUDeviceLostReason reason, WGPUStringView message)
{
    std::string msg = "WGPU device lost: " + std::string(message.data, message.length);
    m_debugCallback->handleMessage(DebugMessageType::Error, DebugMessageSource::Driver, msg.c_str());
}

void DeviceImpl::reportUncapturedError(WGPUErrorType type, WGPUStringView message)
{
    std::string msg = "WGPU uncaptured error: " + std::string(message.data, message.length);
    m_debugCallback->handleMessage(DebugMessageType::Error, DebugMessageSource::Driver, msg.c_str());
    this->m_lastUncapturedError = type;
}

WGPUErrorType DeviceImpl::getAndClearLastUncapturedError()
{
    WGPUErrorType error = this->m_lastUncapturedError;
    this->m_lastUncapturedError = WGPUErrorType_NoError;
    return error;
}

Result DeviceImpl::initialize(const DeviceDesc& desc)
{
    SLANG_RETURN_ON_FAIL(Device::initialize(desc));

    SLANG_RETURN_ON_FAIL(m_ctx.api.init());
    API& api = m_ctx.api;

    SLANG_RETURN_ON_FAIL(createWGPUInstance(api, &m_ctx.instance));
    SLANG_RETURN_ON_FAIL(createWGPUAdapter(api, m_ctx.instance, &m_ctx.adapter));

    // Query adapter limits.
    WGPULimits adapterLimits = {};
    api.wgpuAdapterGetLimits(m_ctx.adapter, &adapterLimits);

    // Query adapter features.
    WGPUSupportedFeatures adapterFeatures = {};
    api.wgpuAdapterGetFeatures(m_ctx.adapter, &adapterFeatures);

    // We request a device with the maximum available limits and feature set.
    WGPULimits requiredLimits = adapterLimits;
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.requiredFeatures = adapterFeatures.features;
    deviceDesc.requiredFeatureCount = adapterFeatures.featureCount;
    deviceDesc.requiredLimits = &requiredLimits;
    deviceDesc.uncapturedErrorCallbackInfo.callback =
        [](const WGPUDevice* device, WGPUErrorType type, WGPUStringView message, void* userdata1, void* userdata2)
    {
        DeviceImpl* deviceImpl = static_cast<DeviceImpl*>(userdata1);
        deviceImpl->reportUncapturedError(type, message);
    };
    deviceDesc.uncapturedErrorCallbackInfo.userdata1 = this;
    WGPUDawnTogglesDescriptor togglesDesc = getDawnTogglesDescriptor();
    deviceDesc.nextInChain = &togglesDesc.chain;

    {
        WGPURequestDeviceStatus status = WGPURequestDeviceStatus(0);
        WGPURequestDeviceCallbackInfo callbackInfo = {};
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WGPURequestDeviceStatus status_,
                                   WGPUDevice device,
                                   WGPUStringView message,
                                   void* userdata1,
                                   void* userdata2)
        {
            *(WGPURequestDeviceStatus*)userdata1 = status_;
            *(WGPUDevice*)userdata2 = device;
        };
        callbackInfo.userdata1 = &status;
        callbackInfo.userdata2 = &m_ctx.device;

        WGPUDeviceLostCallbackInfo deviceLostCallbackInfo = {};
        deviceLostCallbackInfo.callback = [](const WGPUDevice* device,
                                             WGPUDeviceLostReason reason,
                                             WGPUStringView message,
                                             void* userdata1,
                                             void* userdata2)
        {
            if (reason != WGPUDeviceLostReason_Destroyed)
            {
                DeviceImpl* deviceimpl = static_cast<DeviceImpl*>(userdata1);
                deviceimpl->reportDeviceLost(reason, message);
            }
        };
        deviceLostCallbackInfo.userdata1 = this;
        deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        deviceDesc.deviceLostCallbackInfo = deviceLostCallbackInfo;

        WGPUFuture future = m_ctx.api.wgpuAdapterRequestDevice(m_ctx.adapter, &deviceDesc, callbackInfo);
        WGPUFutureWaitInfo futures[1] = {{future}};
        uint64_t timeoutNS = UINT64_MAX;
        WGPUWaitStatus waitStatus =
            m_ctx.api.wgpuInstanceWaitAny(m_ctx.instance, SLANG_COUNT_OF(futures), futures, timeoutNS);
        if (waitStatus != WGPUWaitStatus_Success || status != WGPURequestDeviceStatus_Success)
        {
            return SLANG_FAIL;
        }
    }

    // Query device limits.
    WGPULimits supportedLimits = {};
    api.wgpuDeviceGetLimits(m_ctx.device, &supportedLimits);
    m_ctx.limits = supportedLimits;

    // Query device features.
    WGPUSupportedFeatures supportedFeatures = {};
    api.wgpuDeviceGetFeatures(m_ctx.device, &supportedFeatures);
    m_ctx.features.insert(supportedFeatures.features, supportedFeatures.features + supportedFeatures.featureCount);

    // Initialize device info.
    {
        m_info.deviceType = DeviceType::WGPU;
        m_info.apiName = "WGPU";
        m_info.adapterLUID = {};

        WGPUAdapterInfo wgpuAdapterInfo = {};
        api.wgpuAdapterGetInfo(m_ctx.adapter, &wgpuAdapterInfo);
        m_adapterName = std::string(wgpuAdapterInfo.device.data, wgpuAdapterInfo.device.length);
        m_info.adapterName = m_adapterName.c_str();
    }

    // Initialize device limits.
    {
        DeviceLimits& limits = m_info.limits;
        limits.maxBufferSize = m_ctx.limits.maxBufferSize;
        limits.maxTextureDimension1D = m_ctx.limits.maxTextureDimension1D;
        limits.maxTextureDimension2D = m_ctx.limits.maxTextureDimension2D;
        limits.maxTextureDimension3D = m_ctx.limits.maxTextureDimension3D;
        limits.maxTextureDimensionCube = m_ctx.limits.maxTextureDimension2D;
        limits.maxTextureLayers = m_ctx.limits.maxTextureArrayLayers;
        limits.maxVertexInputElements = m_ctx.limits.maxVertexAttributes;
        limits.maxVertexInputElementOffset = m_ctx.limits.maxVertexBufferArrayStride;
        limits.maxVertexStreams = m_ctx.limits.maxVertexBuffers;
        limits.maxVertexStreamStride = m_ctx.limits.maxVertexBufferArrayStride;
        limits.maxComputeThreadsPerGroup = m_ctx.limits.maxComputeInvocationsPerWorkgroup;
        limits.maxComputeThreadGroupSize[0] = m_ctx.limits.maxComputeWorkgroupSizeX;
        limits.maxComputeThreadGroupSize[1] = m_ctx.limits.maxComputeWorkgroupSizeY;
        limits.maxComputeThreadGroupSize[2] = m_ctx.limits.maxComputeWorkgroupSizeZ;
        limits.maxComputeDispatchThreadGroups[0] = m_ctx.limits.maxComputeWorkgroupsPerDimension;
        limits.maxComputeDispatchThreadGroups[1] = m_ctx.limits.maxComputeWorkgroupsPerDimension;
        limits.maxComputeDispatchThreadGroups[2] = m_ctx.limits.maxComputeWorkgroupsPerDimension;
        limits.maxViewports = 1;
        limits.maxViewportDimensions[0] = m_ctx.limits.maxTextureDimension2D;
        limits.maxViewportDimensions[1] = m_ctx.limits.maxTextureDimension2D;
        limits.maxFramebufferDimensions[0] = m_ctx.limits.maxTextureDimension2D;
        limits.maxFramebufferDimensions[1] = m_ctx.limits.maxTextureDimension2D;
        limits.maxFramebufferDimensions[2] = m_ctx.limits.maxTextureArrayLayers;
        limits.maxShaderVisibleSamplers = m_ctx.limits.maxSamplersPerShaderStage;
    }

    // Initialize features & capabilities.
    addFeature(Feature::HardwareDevice);
    addFeature(Feature::Surface);
    addFeature(Feature::ParameterBlock);
    addFeature(Feature::Rasterization);
    if (api.wgpuDeviceHasFeature(m_ctx.device, WGPUFeatureName_ShaderF16))
    {
        addFeature(Feature::Half);
    }

    addCapability(Capability::wgsl);

    // Initialize format support table.
    initializeFormatSupport();

    // Initialize slang context.
    SLANG_RETURN_ON_FAIL(m_slangContext.initialize(
        desc.slang,
        SLANG_WGSL,
        nullptr,
        getCapabilities(),
        std::array{slang::PreprocessorMacroDesc{"__WGPU__", "1"}}
    ));

    // Create queue.
    m_queue = new CommandQueueImpl(this, QueueType::Graphics);
    m_queue->setInternalReferenceCount(1);

    return SLANG_OK;
}

void DeviceImpl::initializeFormatSupport()
{
    // WebGPU format support table based on the spec:
    // https://www.w3.org/TR/webgpu/#texture-format-caps

    API& api = m_ctx.api;
    bool supportDepth32FloatStencil8 = api.wgpuDeviceHasFeature(m_ctx.device, WGPUFeatureName_Depth32FloatStencil8);
    bool supportBC = api.wgpuDeviceHasFeature(m_ctx.device, WGPUFeatureName_TextureCompressionBC);
    bool supportBGRA8UnormStorage = api.wgpuDeviceHasFeature(m_ctx.device, WGPUFeatureName_BGRA8UnormStorage);
    bool supportFloat32Filterable = api.wgpuDeviceHasFeature(m_ctx.device, WGPUFeatureName_Float32Filterable);
    bool supportFloat32Blendable = true; // api.wgpuDeviceHasFeature(m_ctx.device, WGPUFeatureName_Float32Blendable);
    bool supportRG11B10UfloatRenderable =
        api.wgpuDeviceHasFeature(m_ctx.device, WGPUFeatureName_RG11B10UfloatRenderable);

    // clang-format off
#define FLOAT               0x0001 // GPUTextureSampleType "float"
#define UNFILTERABLE_FLOAT  0x0002 // GPUTextureSampleType "unfilterable-float"
#define UINT                0x0004 // GPUTextureSampleType "uint"
#define SINT                0x0008 // GPUTextureSampleType "sint"
#define DEPTH               0x0010 // GPUTextureSampleType "depth"
#define COPY_SRC            0x0020 // "copy-src"
#define COPY_DST            0x0040 // "copy-dst"
#define RENDER              0x0080 // "RENDER_ATTACHMENT"
#define BLENDABLE           0x0100 // "blendable"
#define MULTISAMPLING       0x0200 // "multisampling"
#define RESOLVE             0x0400 // "resolve"
#define STORAGE_WO          0x0800 // "STORAGE_BINDING" write-only
#define STORAGE_RO          0x1000 // "STORAGE_BINDING" read-only
#define STORAGE_RW          0x2000 // "STORAGE_BINDING" read-write
    // clang-format on

    auto set = [&](Format format, int flags, bool supported = true)
    {
        if (flags == 0 || !supported)
        {
            return;
        }

        // Add flags depending on feature support.
        if (format == Format::BGRA8UnormSrgb)
        {
            flags |= supportBGRA8UnormStorage ? STORAGE_WO : 0;
        }
        if (format == Format::R32Float || format == Format::RG32Float || format == Format::RGBA32Float)
        {
            flags |= supportFloat32Filterable ? FLOAT : 0;
            flags |= supportFloat32Blendable ? BLENDABLE : 0;
        }
        if (format == Format::R11G11B10Float)
        {
            flags |= supportRG11B10UfloatRenderable ? (RENDER | BLENDABLE | MULTISAMPLING | RESOLVE) : 0;
        }

        FormatSupport support = FormatSupport::None;
        support |= (flags & COPY_SRC) ? FormatSupport::CopySource : FormatSupport::None;
        support |= (flags & COPY_DST) ? FormatSupport::CopyDestination : FormatSupport::None;
        support |= FormatSupport::Texture;
        if (flags & RENDER)
        {
            support |= (flags & DEPTH) ? FormatSupport::DepthStencil : FormatSupport::RenderTarget;
        }
        support |= (flags & MULTISAMPLING) ? FormatSupport::Multisampling : FormatSupport::None;
        support |= (flags & BLENDABLE) ? FormatSupport::Blendable : FormatSupport::None;
        support |= (flags & RESOLVE) ? FormatSupport::Resolvable : FormatSupport::None;

        support |= FormatSupport::ShaderLoad;
        support |= FormatSupport::ShaderSample;
        support |= ((flags & STORAGE_WO) || (flags & STORAGE_RW)) ? FormatSupport::ShaderUavStore : FormatSupport::None;
        support |= ((flags & STORAGE_RO) || (flags & STORAGE_RW)) ? FormatSupport::ShaderUavLoad : FormatSupport::None;

        if (translateVertexFormat(format) != WGPUVertexFormat(0))
        {
            support |= FormatSupport::VertexBuffer;
        }
        if (format == Format::R32Uint || format == Format::R16Uint)
        {
            support |= FormatSupport::IndexBuffer;
        }

        m_formatSupport[size_t(format)] = support;
    };

    // clang-format off
    set(Format::R8Uint,         UINT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING);
    set(Format::R8Sint,         SINT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING);
    set(Format::R8Unorm,        FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | RENDER | BLENDABLE | MULTISAMPLING | RESOLVE);
    set(Format::R8Snorm,        FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST);

    set(Format::RG8Uint,        UINT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING);
    set(Format::RG8Sint,        SINT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING);
    set(Format::RG8Unorm,       FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | RENDER | BLENDABLE | MULTISAMPLING | RESOLVE);
    set(Format::RG8Snorm,       FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST);

    set(Format::RGBA8Uint,      UINT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING | STORAGE_WO | STORAGE_RO);
    set(Format::RGBA8Sint,      SINT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING | STORAGE_WO | STORAGE_RO);
    set(Format::RGBA8Unorm,     FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | RENDER | BLENDABLE | MULTISAMPLING | RESOLVE | STORAGE_WO | STORAGE_RO);
    set(Format::RGBA8UnormSrgb, FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | RENDER | BLENDABLE | MULTISAMPLING | RESOLVE);
    set(Format::RGBA8Snorm,     FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | STORAGE_WO | STORAGE_RO);

    set(Format::BGRA8Unorm,     FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | RENDER | BLENDABLE | MULTISAMPLING | RESOLVE);
    set(Format::BGRA8UnormSrgb, FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | RENDER | BLENDABLE | MULTISAMPLING | RESOLVE); // STORAGE_WO if supportBGRA8UnormStorage
    set(Format::BGRX8Unorm,     0);
    set(Format::BGRX8UnormSrgb, 0);

    set(Format::R16Uint,        UINT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING);
    set(Format::R16Sint,        SINT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING);
    set(Format::R16Unorm,       0);
    set(Format::R16Snorm,       0);
    set(Format::R16Float,       FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | RENDER | BLENDABLE | MULTISAMPLING | RESOLVE);

    set(Format::RG16Uint,       UINT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING);
    set(Format::RG16Sint,       SINT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING);
    set(Format::RG16Unorm,      0);
    set(Format::RG16Snorm,      0);
    set(Format::RG16Float,      FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | RENDER | BLENDABLE | MULTISAMPLING | RESOLVE);

    set(Format::RGBA16Uint,     UINT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING | STORAGE_WO | STORAGE_RO);
    set(Format::RGBA16Sint,     SINT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING | STORAGE_WO | STORAGE_RO);
    set(Format::RGBA16Unorm,    0);
    set(Format::RGBA16Snorm,    0);
    set(Format::RGBA16Float,    FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | RENDER | BLENDABLE | MULTISAMPLING | RESOLVE | STORAGE_WO | STORAGE_RO);

    set(Format::R32Uint,        UINT | COPY_SRC | COPY_DST | RENDER | STORAGE_WO | STORAGE_RO | STORAGE_RW);
    set(Format::R32Sint,        SINT | COPY_SRC | COPY_DST | RENDER | STORAGE_WO | STORAGE_RO | STORAGE_RW);
    set(Format::R32Float,       UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING | STORAGE_WO | STORAGE_RO | STORAGE_RW); // FLOAT if supportFloat32Filterable, BLENDABLE if supportFloat32Blendable

    set(Format::RG32Uint,       UINT | COPY_SRC | COPY_DST | RENDER | STORAGE_WO | STORAGE_RO);
    set(Format::RG32Sint,       SINT | COPY_SRC | COPY_DST | RENDER | STORAGE_WO | STORAGE_RO);
    set(Format::RG32Float,      UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | RENDER | STORAGE_WO | STORAGE_RO); // FLOAT if supportFloat32Filterable, BLENDABLE if supportFloat32Blendable

    set(Format::RGB32Uint,      0);
    set(Format::RGB32Sint,      0);
    set(Format::RGB32Float,     0);

    set(Format::RGBA32Uint,     UINT | COPY_SRC | COPY_DST | RENDER | STORAGE_WO | STORAGE_RO);
    set(Format::RGBA32Sint,     SINT | COPY_SRC | COPY_DST | RENDER | STORAGE_WO | STORAGE_RO);
    set(Format::RGBA32Float,    UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | RENDER | STORAGE_WO | STORAGE_RO); // FLOAT if supportFloat32Filterable, BLENDABLE if supportFloat32Blendable

    set(Format::R64Uint,        0);
    set(Format::R64Sint,        0);

    set(Format::BGRA4Unorm,     0);
    set(Format::B5G6R5Unorm,    0);
    set(Format::BGR5A1Unorm,    0);

    set(Format::RGB9E5Ufloat,   FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST);
    set(Format::RGB10A2Uint,    UINT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING);
    set(Format::RGB10A2Unorm,   FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | RENDER | BLENDABLE | MULTISAMPLING | RESOLVE);
    set(Format::R11G11B10Float, FLOAT | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST); // RENDER | BLENDABLE | MULTISAMPLING | RESOLVE if supportRG11B10UfloatRenderable

    set(Format::D32Float,       DEPTH | COPY_SRC | RENDER | MULTISAMPLING);
    set(Format::D16Unorm,       DEPTH | UNFILTERABLE_FLOAT | COPY_SRC | COPY_DST | RENDER | MULTISAMPLING);
    set(Format::D32FloatS8Uint, DEPTH | UNFILTERABLE_FLOAT | RENDER | MULTISAMPLING, supportDepth32FloatStencil8);

    set(Format::BC1Unorm,       FLOAT | COPY_SRC | COPY_DST, supportBC);
    set(Format::BC1UnormSrgb,   FLOAT | COPY_SRC | COPY_DST, supportBC);
    set(Format::BC2Unorm,       FLOAT | COPY_SRC | COPY_DST, supportBC);
    set(Format::BC2UnormSrgb,   FLOAT | COPY_SRC | COPY_DST, supportBC);
    set(Format::BC3Unorm,       FLOAT | COPY_SRC | COPY_DST, supportBC);
    set(Format::BC3UnormSrgb,   FLOAT | COPY_SRC | COPY_DST, supportBC);
    set(Format::BC4Unorm,       FLOAT | COPY_SRC | COPY_DST, supportBC);
    set(Format::BC4Snorm,       FLOAT | COPY_SRC | COPY_DST, supportBC);
    set(Format::BC5Unorm,       FLOAT | COPY_SRC | COPY_DST, supportBC);
    set(Format::BC5Snorm,       FLOAT | COPY_SRC | COPY_DST, supportBC);
    set(Format::BC6HUfloat,     FLOAT | COPY_SRC | COPY_DST, supportBC);
    set(Format::BC6HSfloat,     FLOAT | COPY_SRC | COPY_DST, supportBC);
    set(Format::BC7Unorm,       FLOAT | COPY_SRC | COPY_DST, supportBC);
    set(Format::BC7UnormSrgb,   FLOAT | COPY_SRC | COPY_DST, supportBC);
    // clang-format on
}

Result DeviceImpl::readBuffer(IBuffer* buffer, Offset offset, Size size, void* outData)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (offset + size > bufferImpl->m_desc.size)
    {
        return SLANG_FAIL;
    }

    WGPUBufferDescriptor stagingBufferDesc = {};
    stagingBufferDesc.size = size;
    stagingBufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer stagingBuffer = m_ctx.api.wgpuDeviceCreateBuffer(m_ctx.device, &stagingBufferDesc);
    if (!stagingBuffer)
    {
        return SLANG_FAIL;
    }
    SLANG_RHI_DEFERRED({ m_ctx.api.wgpuBufferRelease(stagingBuffer); });

    WGPUCommandEncoder encoder = m_ctx.api.wgpuDeviceCreateCommandEncoder(m_ctx.device, nullptr);
    if (!encoder)
    {
        return SLANG_FAIL;
    }
    SLANG_RHI_DEFERRED({ m_ctx.api.wgpuCommandEncoderRelease(encoder); });

    m_ctx.api.wgpuCommandEncoderCopyBufferToBuffer(encoder, bufferImpl->m_buffer, offset, stagingBuffer, 0, size);
    WGPUCommandBuffer commandBuffer = m_ctx.api.wgpuCommandEncoderFinish(encoder, nullptr);
    if (!commandBuffer)
    {
        return SLANG_FAIL;
    }
    SLANG_RHI_DEFERRED({ m_ctx.api.wgpuCommandBufferRelease(commandBuffer); });

    WGPUQueue queue = m_ctx.api.wgpuDeviceGetQueue(m_ctx.device);
    SLANG_RHI_DEFERRED({ m_ctx.api.wgpuQueueRelease(queue); });
    m_ctx.api.wgpuQueueSubmit(queue, 1, &commandBuffer);

    // Wait for the command buffer to finish executing
    {
        WGPUQueueWorkDoneStatus status = WGPUQueueWorkDoneStatus(0);
        WGPUQueueWorkDoneCallbackInfo callbackInfo = {};
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WGPUQueueWorkDoneStatus status_, void* userdata1, void* userdata2)
        {
            *(WGPUQueueWorkDoneStatus*)userdata1 = status_;
        };
        callbackInfo.userdata1 = &status;
        WGPUFuture future = m_ctx.api.wgpuQueueOnSubmittedWorkDone(queue, callbackInfo);
        WGPUFutureWaitInfo futures[1] = {{future}};
        uint64_t timeoutNS = UINT64_MAX;
        WGPUWaitStatus waitStatus =
            m_ctx.api.wgpuInstanceWaitAny(m_ctx.instance, SLANG_COUNT_OF(futures), futures, timeoutNS);
        if (waitStatus != WGPUWaitStatus_Success || status != WGPUQueueWorkDoneStatus_Success)
        {
            return SLANG_FAIL;
        }
    }

    // Map the staging buffer
    {
        WGPUMapAsyncStatus status = WGPUMapAsyncStatus(0);
        WGPUBufferMapCallbackInfo callbackInfo = {};
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WGPUMapAsyncStatus status_, WGPUStringView message, void* userdata1, void* userdata2)
        {
            *(WGPUMapAsyncStatus*)userdata1 = status_;
            if (status_ != WGPUMapAsyncStatus_Success)
            {
                static_cast<DeviceImpl*>(userdata2)->reportError("wgpuBufferMapAsync", message);
            }
        };
        callbackInfo.userdata1 = &status;
        callbackInfo.userdata2 = this;
        WGPUFuture future = m_ctx.api.wgpuBufferMapAsync(stagingBuffer, WGPUMapMode_Read, 0, size, callbackInfo);
        WGPUFutureWaitInfo futures[1] = {{future}};
        uint64_t timeoutNS = UINT64_MAX;
        WGPUWaitStatus waitStatus =
            m_ctx.api.wgpuInstanceWaitAny(m_ctx.instance, SLANG_COUNT_OF(futures), futures, timeoutNS);
        if (waitStatus != WGPUWaitStatus_Success || status != WGPUMapAsyncStatus_Success)
        {
            return SLANG_FAIL;
        }
    }
    SLANG_RHI_DEFERRED({ m_ctx.api.wgpuBufferUnmap(stagingBuffer); });

    const void* data = m_ctx.api.wgpuBufferGetConstMappedRange(stagingBuffer, 0, size);
    if (!data)
    {
        return SLANG_FAIL;
    }

    std::memcpy(outData, data, size);

    return SLANG_OK;
}

Result DeviceImpl::getTextureAllocationInfo(const TextureDesc& desc, Size* outSize, Size* outAlignment)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result DeviceImpl::getTextureRowAlignment(Format format, Size* outAlignment)
{
    *outAlignment = 256;
    return SLANG_OK;
}

Result DeviceImpl::createShaderObjectLayout(
    slang::ISession* session,
    slang::TypeLayoutReflection* typeLayout,
    ShaderObjectLayout** outLayout
)
{
    RefPtr<ShaderObjectLayoutImpl> layout;
    SLANG_RETURN_ON_FAIL(ShaderObjectLayoutImpl::createForElementType(this, session, typeLayout, layout.writeRef()));
    returnRefPtrMove(outLayout, layout);
    return SLANG_OK;
}

Result DeviceImpl::createRootShaderObjectLayout(
    slang::IComponentType* program,
    slang::ProgramLayout* programLayout,
    ShaderObjectLayout** outLayout
)
{
    return SLANG_FAIL;
}

Result DeviceImpl::createShaderTable(const ShaderTableDesc& desc, IShaderTable** outShaderTable)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

inline Result getAdaptersImpl(std::vector<Adapter>& outAdapters)
{
    // If WGPU is not available, return no adapters.
    API api;
    if (SLANG_FAILED(api.init()))
    {
        return SLANG_OK;
    }

    WGPUInstance wgpuInstance = {};
    SLANG_RETURN_ON_FAIL(createWGPUInstance(api, &wgpuInstance));
    SLANG_RHI_DEFERRED({ api.wgpuInstanceRelease(wgpuInstance); });

    WGPUAdapter wgpuAdapter = {};
    SLANG_RETURN_ON_FAIL(createWGPUAdapter(api, wgpuInstance, &wgpuAdapter));
    SLANG_RHI_DEFERRED({ api.wgpuAdapterRelease(wgpuAdapter); });

    WGPUAdapterInfo wgpuAdapterInfo = {};
    api.wgpuAdapterGetInfo(wgpuAdapter, &wgpuAdapterInfo);

    AdapterInfo info = {};
    info.deviceType = DeviceType::WGPU;
    switch (wgpuAdapterInfo.adapterType)
    {
    case WGPUAdapterType_DiscreteGPU:
        info.adapterType = AdapterType::Discrete;
        break;
    case WGPUAdapterType_IntegratedGPU:
        info.adapterType = AdapterType::Integrated;
        break;
    case WGPUAdapterType_CPU:
        info.adapterType = AdapterType::Software;
        break;
    default:
        info.adapterType = AdapterType::Unknown;
        break;
    }
    string::copy_safe(info.name, sizeof(info.name), wgpuAdapterInfo.device.data, wgpuAdapterInfo.device.length);
    info.vendorID = wgpuAdapterInfo.vendorID;
    info.deviceID = wgpuAdapterInfo.deviceID;

    Adapter adapter;
    adapter.m_info = info;
    adapter.m_isDefault = true;
    outAdapters.push_back(adapter);

    return SLANG_OK;
}

std::vector<Adapter>& getAdapters()
{
    static std::vector<Adapter> adapters;
    static Result initResult = getAdaptersImpl(adapters);
    SLANG_UNUSED(initResult);
    return adapters;
}

} // namespace rhi::wgpu

namespace rhi {

IAdapter* getWGPUAdapter(uint32_t index)
{
    std::vector<Adapter>& adapters = wgpu::getAdapters();
    return index < adapters.size() ? &adapters[index] : nullptr;
}

Result createWGPUDevice(const DeviceDesc* desc, IDevice** outDevice)
{
    RefPtr<wgpu::DeviceImpl> result = new wgpu::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outDevice, result);
    return SLANG_OK;
}

} // namespace rhi
