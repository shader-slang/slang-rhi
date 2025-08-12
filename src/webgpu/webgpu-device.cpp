#include "webgpu-device.h"
#include "webgpu-buffer.h"
#include "webgpu-texture.h"
#include "webgpu-shader-program.h"
#include "webgpu-shader-object.h"
#include "webgpu-shader-object-layout.h"
#include "webgpu-utils.h"

#include "core/common.h"
#include "core/deferred.h"

#include <cstdio>
#include <vector>

namespace rhi::webgpu {

static void errorCallback(WebGPUErrorType type, const char* message, void* userdata)
{
    DeviceImpl* device = static_cast<DeviceImpl*>(userdata);
    device->handleError(type, message);
}

Context::~Context()
{
    if (device)
    {
        api.webgpuDeviceRelease(device);
    }
    if (adapter)
    {
        api.webgpuAdapterRelease(adapter);
    }
    if (instance)
    {
        api.webgpuInstanceRelease(instance);
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

void DeviceImpl::handleError(WebGPUErrorType type, const char* message)
{
    fprintf(stderr, "WebGPU error: %s\n", message);
    this->m_lastError = type;
}

WebGPUErrorType DeviceImpl::getAndClearLastError()
{
    WebGPUErrorType lastError = this->m_lastError;
    this->m_lastError = WebGPUErrorType_NoError;
    return lastError;
}

Result DeviceImpl::initialize(const DeviceDesc& desc)
{
    SLANG_RETURN_ON_FAIL(Device::initialize(desc));

    SLANG_RETURN_ON_FAIL(m_ctx.api.init());
    API& api = m_ctx.api;

    const std::vector<const char*> enabledToggles = {"use_dxc"};
    WebGPUDawnTogglesDescriptor togglesDesc = {};
    togglesDesc.chain.sType = WebGPUSType_DawnTogglesDescriptor;
    togglesDesc.enabledToggleCount = enabledToggles.size();
    togglesDesc.enabledToggles = enabledToggles.data();

    WebGPUInstanceDescriptor instanceDesc = {};
    instanceDesc.features.timedWaitAnyEnable = WebGPUBool(true);
    instanceDesc.nextInChain = &togglesDesc.chain;
    m_ctx.instance = api.webgpuCreateInstance(&instanceDesc);

    // Request adapter.
    WebGPURequestAdapterOptions options = {};
    options.powerPreference = WebGPUPowerPreference_HighPerformance;
#if SLANG_WINDOWS_FAMILY
    // TODO(webgpu-d3d): New validation error in D3D kills webgpu, so use vulkan for now.
    options.backendType = WebGPUBackendType_Vulkan;
#elif SLANG_LINUX_FAMILY
    options.backendType = WebGPUBackendType_Vulkan;
#endif
    options.nextInChain = &togglesDesc.chain;

    {
        WebGPURequestAdapterStatus status = WebGPURequestAdapterStatus_Unknown;
        WebGPURequestAdapterCallbackInfo2 callbackInfo = {};
        callbackInfo.mode = WebGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WebGPURequestAdapterStatus status_,
                                   WebGPUAdapter adapter,
                                   const char* message,
                                   void* userdata1,
                                   void* userdata2)
        {
            *(WebGPURequestAdapterStatus*)userdata1 = status_;
            *(WebGPUAdapter*)userdata2 = adapter;
        };
        callbackInfo.userdata1 = &status;
        callbackInfo.userdata2 = &m_ctx.adapter;
        WebGPUFuture future = m_ctx.api.webgpuInstanceRequestAdapter2(m_ctx.instance, &options, callbackInfo);
        WebGPUFutureWaitInfo futures[1] = {{future}};
        uint64_t timeoutNS = UINT64_MAX;
        WebGPUWaitStatus waitStatus =
            m_ctx.api.webgpuInstanceWaitAny(m_ctx.instance, SLANG_COUNT_OF(futures), futures, timeoutNS);
        if (waitStatus != WebGPUWaitStatus_Success || status != WebGPURequestAdapterStatus_Success)
        {
            return SLANG_FAIL;
        }
    }

    // Query adapter limits.
    WebGPUSupportedLimits adapterLimits = {};
    api.webgpuAdapterGetLimits(m_ctx.adapter, &adapterLimits);

    // Query adapter features.
    size_t adapterFeatureCount = api.webgpuAdapterEnumerateFeatures(m_ctx.adapter, nullptr);
    std::vector<WebGPUFeatureName> adapterFeatures(adapterFeatureCount);
    api.webgpuAdapterEnumerateFeatures(m_ctx.adapter, adapterFeatures.data());

    // We request a device with the maximum available limits and feature set.
    WebGPURequiredLimits requiredLimits = {};
    requiredLimits.limits = adapterLimits.limits;
    WebGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.requiredFeatures = adapterFeatures.data();
    deviceDesc.requiredFeatureCount = adapterFeatures.size();
    deviceDesc.requiredLimits = &requiredLimits;
    deviceDesc.uncapturedErrorCallbackInfo.callback = errorCallback;
    deviceDesc.uncapturedErrorCallbackInfo.userdata = this;
    deviceDesc.nextInChain = &togglesDesc.chain;

    {
        WebGPURequestDeviceStatus status = WebGPURequestDeviceStatus_Unknown;
        WebGPURequestDeviceCallbackInfo2 callbackInfo = {};
        callbackInfo.mode = WebGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WebGPURequestDeviceStatus status_,
                                   WebGPUDevice device,
                                   const char* message,
                                   void* userdata1,
                                   void* userdata2)
        {
            *(WebGPURequestDeviceStatus*)userdata1 = status_;
            *(WebGPUDevice*)userdata2 = device;
        };
        callbackInfo.userdata1 = &status;
        callbackInfo.userdata2 = &m_ctx.device;

        WebGPUDeviceLostCallbackInfo2 deviceLostCallbackInfo = {};
        deviceLostCallbackInfo.callback = [](const WebGPUDevice* device,
                                             WebGPUDeviceLostReason reason,
                                             const char* message,
                                             void* userdata1,
                                             void* userdata2)
        {
            if (reason != WebGPUDeviceLostReason_Destroyed)
            {
                DeviceImpl* deviceimpl = static_cast<DeviceImpl*>(userdata1);
                deviceimpl->handleError(WebGPUErrorType_DeviceLost, message);
            }
        };
        deviceLostCallbackInfo.userdata1 = this;
        deviceLostCallbackInfo.mode = WebGPUCallbackMode_AllowSpontaneous;
        deviceDesc.deviceLostCallbackInfo2 = deviceLostCallbackInfo;

        WebGPUFuture future = m_ctx.api.webgpuAdapterRequestDevice2(m_ctx.adapter, &deviceDesc, callbackInfo);
        WebGPUFutureWaitInfo futures[1] = {{future}};
        uint64_t timeoutNS = UINT64_MAX;
        WebGPUWaitStatus waitStatus =
            m_ctx.api.webgpuInstanceWaitAny(m_ctx.instance, SLANG_COUNT_OF(futures), futures, timeoutNS);
        if (waitStatus != WebGPUWaitStatus_Success || status != WebGPURequestDeviceStatus_Success)
        {
            return SLANG_FAIL;
        }
    }

    // Query device limits.
    WebGPUSupportedLimits supportedLimits = {};
    api.webgpuDeviceGetLimits(m_ctx.device, &supportedLimits);
    m_ctx.limits = supportedLimits.limits;

    // Query device features.
    size_t deviceFeatureCount = api.webgpuDeviceEnumerateFeatures(m_ctx.device, nullptr);
    std::vector<WebGPUFeatureName> deviceFeatures(deviceFeatureCount);
    api.webgpuDeviceEnumerateFeatures(m_ctx.device, deviceFeatures.data());
    m_ctx.features.insert(deviceFeatures.begin(), deviceFeatures.end());

    // Initialize device info.
    {
        m_info.deviceType = DeviceType::WebGPU;
        m_info.apiName = "WebGPU";
        m_info.adapterName = "default";
        m_info.adapterLUID = {};
    }

    // Initialize device limits.
    {
        m_info.limits.maxTextureDimension1D = m_ctx.limits.maxTextureDimension1D;
        m_info.limits.maxTextureDimension2D = m_ctx.limits.maxTextureDimension2D;
        m_info.limits.maxTextureDimension3D = m_ctx.limits.maxTextureDimension3D;
        m_info.limits.maxTextureDimensionCube = m_ctx.limits.maxTextureDimension2D;
        m_info.limits.maxTextureLayers = m_ctx.limits.maxTextureArrayLayers;
        m_info.limits.maxVertexInputElements = m_ctx.limits.maxVertexAttributes;
        m_info.limits.maxVertexInputElementOffset = m_ctx.limits.maxVertexBufferArrayStride;
        m_info.limits.maxVertexStreams = m_ctx.limits.maxVertexBuffers;
        m_info.limits.maxVertexStreamStride = m_ctx.limits.maxVertexBufferArrayStride;
        m_info.limits.maxComputeThreadsPerGroup = m_ctx.limits.maxComputeInvocationsPerWorkgroup;
        m_info.limits.maxComputeThreadGroupSize[0] = m_ctx.limits.maxComputeWorkgroupSizeX;
        m_info.limits.maxComputeThreadGroupSize[1] = m_ctx.limits.maxComputeWorkgroupSizeY;
        m_info.limits.maxComputeThreadGroupSize[2] = m_ctx.limits.maxComputeWorkgroupSizeZ;
        m_info.limits.maxComputeDispatchThreadGroups[0] = m_ctx.limits.maxComputeWorkgroupsPerDimension;
        m_info.limits.maxComputeDispatchThreadGroups[1] = m_ctx.limits.maxComputeWorkgroupsPerDimension;
        m_info.limits.maxComputeDispatchThreadGroups[2] = m_ctx.limits.maxComputeWorkgroupsPerDimension;
        // m_info.limits.maxViewports
        // m_info.limits.maxViewportDimensions[2]
        // m_info.limits.maxFramebufferDimensions[3]
        m_info.limits.maxShaderVisibleSamplers = m_ctx.limits.maxSamplersPerShaderStage;
    }

    // Initialize features & capabilities.
    addFeature(Feature::HardwareDevice);
    addFeature(Feature::Surface);
    addFeature(Feature::ParameterBlock);
    addFeature(Feature::Rasterization);
    if (api.webgpuDeviceHasFeature(m_ctx.device, WebGPUFeatureName_ShaderF16))
    {
        addFeature(Feature::Half);
    }

    addCapability(Capability::wgsl);

    // Initialize format support table.
    initializeFormatSupport();

    // Initialize slang context.
    SLANG_RETURN_ON_FAIL(
        m_slangContext.initialize(desc.slang, SLANG_WGSL, "", std::array{slang::PreprocessorMacroDesc{"__WEBGPU__", "1"}})
    );

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
    bool supportDepth32FloatStencil8 = api.webgpuDeviceHasFeature(m_ctx.device, WebGPUFeatureName_Depth32FloatStencil8);
    bool supportBC = api.webgpuDeviceHasFeature(m_ctx.device, WebGPUFeatureName_TextureCompressionBC);
    bool supportBGRA8UnormStorage = api.webgpuDeviceHasFeature(m_ctx.device, WebGPUFeatureName_BGRA8UnormStorage);
    bool supportFloat32Filterable = api.webgpuDeviceHasFeature(m_ctx.device, WebGPUFeatureName_Float32Filterable);
    bool supportFloat32Blendable = true; // api.webgpuDeviceHasFeature(m_ctx.device, WebGPUFeatureName_Float32Blendable);
    bool supportRG11B10UfloatRenderable =
        api.webgpuDeviceHasFeature(m_ctx.device, WebGPUFeatureName_RG11B10UfloatRenderable);

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

        if (translateVertexFormat(format) != WebGPUVertexFormat(0))
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

    WebGPUBufferDescriptor stagingBufferDesc = {};
    stagingBufferDesc.size = size;
    stagingBufferDesc.usage = WebGPUBufferUsage_CopyDst | WebGPUBufferUsage_MapRead;
    WebGPUBuffer stagingBuffer = m_ctx.api.webgpuDeviceCreateBuffer(m_ctx.device, &stagingBufferDesc);
    if (!stagingBuffer)
    {
        return SLANG_FAIL;
    }
    SLANG_RHI_DEFERRED({ m_ctx.api.webgpuBufferRelease(stagingBuffer); });

    WebGPUCommandEncoder encoder = m_ctx.api.webgpuDeviceCreateCommandEncoder(m_ctx.device, nullptr);
    if (!encoder)
    {
        return SLANG_FAIL;
    }
    SLANG_RHI_DEFERRED({ m_ctx.api.webgpuCommandEncoderRelease(encoder); });

    m_ctx.api.webgpuCommandEncoderCopyBufferToBuffer(encoder, bufferImpl->m_buffer, offset, stagingBuffer, 0, size);
    WebGPUCommandBuffer commandBuffer = m_ctx.api.webgpuCommandEncoderFinish(encoder, nullptr);
    if (!commandBuffer)
    {
        return SLANG_FAIL;
    }
    SLANG_RHI_DEFERRED({ m_ctx.api.webgpuCommandBufferRelease(commandBuffer); });

    WebGPUQueue queue = m_ctx.api.webgpuDeviceGetQueue(m_ctx.device);
    SLANG_RHI_DEFERRED({ m_ctx.api.webgpuQueueRelease(queue); });
    m_ctx.api.webgpuQueueSubmit(queue, 1, &commandBuffer);

    // Wait for the command buffer to finish executing
    {
        WebGPUQueueWorkDoneStatus status = WebGPUQueueWorkDoneStatus_Unknown;
        WebGPUQueueWorkDoneCallbackInfo2 callbackInfo = {};
        callbackInfo.mode = WebGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WebGPUQueueWorkDoneStatus status_, void* userdata1, void* userdata2)
        { *(WebGPUQueueWorkDoneStatus*)userdata1 = status_; };
        callbackInfo.userdata1 = &status;
        WebGPUFuture future = m_ctx.api.webgpuQueueOnSubmittedWorkDone2(queue, callbackInfo);
        WebGPUFutureWaitInfo futures[1] = {{future}};
        uint64_t timeoutNS = UINT64_MAX;
        WebGPUWaitStatus waitStatus =
            m_ctx.api.webgpuInstanceWaitAny(m_ctx.instance, SLANG_COUNT_OF(futures), futures, timeoutNS);
        if (waitStatus != WebGPUWaitStatus_Success || status != WebGPUQueueWorkDoneStatus_Success)
        {
            return SLANG_FAIL;
        }
    }

    // Map the staging buffer
    {
        WebGPUMapAsyncStatus status = WebGPUMapAsyncStatus_Unknown;
        WebGPUBufferMapCallbackInfo2 callbackInfo = {};
        callbackInfo.mode = WebGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WebGPUMapAsyncStatus status_, const char* message, void* userdata1, void* userdata2)
        { *(WebGPUMapAsyncStatus*)userdata1 = status_; };
        callbackInfo.userdata1 = &status;
        WebGPUFuture future = m_ctx.api.webgpuBufferMapAsync2(stagingBuffer, WebGPUMapMode_Read, 0, size, callbackInfo);
        WebGPUFutureWaitInfo futures[1] = {{future}};
        uint64_t timeoutNS = UINT64_MAX;
        WebGPUWaitStatus waitStatus =
            m_ctx.api.webgpuInstanceWaitAny(m_ctx.instance, SLANG_COUNT_OF(futures), futures, timeoutNS);
        if (waitStatus != WebGPUWaitStatus_Success || status != WebGPUMapAsyncStatus_Success)
        {
            return SLANG_FAIL;
        }
    }
    SLANG_RHI_DEFERRED({ m_ctx.api.webgpuBufferUnmap(stagingBuffer); });

    const void* data = m_ctx.api.webgpuBufferGetConstMappedRange(stagingBuffer, 0, size);
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

} // namespace rhi::webgpu

namespace rhi {

Result SLANG_MCALL getWebGPUAdapters(std::vector<AdapterInfo>& outAdapters)
{
    // TODO: implement
    return SLANG_OK;
}

Result SLANG_MCALL createWebGPUDevice(const DeviceDesc* desc, IDevice** outRenderer)
{
    RefPtr<webgpu::DeviceImpl> result = new webgpu::DeviceImpl();
    SLANG_RETURN_ON_FAIL(result->initialize(*desc));
    returnComPtr(outRenderer, result);
    return SLANG_OK;
}

} // namespace rhi
