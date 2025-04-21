#include "wgpu-device.h"
#include "wgpu-buffer.h"
#include "wgpu-texture.h"
#include "wgpu-shader-program.h"
#include "wgpu-shader-object.h"
#include "wgpu-shader-object-layout.h"
#include "wgpu-util.h"

#include "core/common.h"
#include "core/deferred.h"

#include <cstdio>
#include <vector>

namespace rhi::wgpu {

static void errorCallback(WGPUErrorType type, const char* message, void* userdata)
{
    DeviceImpl* device = static_cast<DeviceImpl*>(userdata);
    device->handleError(type, message);
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

void DeviceImpl::handleError(WGPUErrorType type, const char* message)
{
    fprintf(stderr, "WGPU error: %s\n", message);
    this->m_lastError = type;
}

WGPUErrorType DeviceImpl::getAndClearLastError()
{
    WGPUErrorType lastError = this->m_lastError;
    this->m_lastError = WGPUErrorType_NoError;
    return lastError;
}

Result DeviceImpl::initialize(const DeviceDesc& desc)
{
    SLANG_RETURN_ON_FAIL(m_ctx.api.init());
    API& api = m_ctx.api;

    // Initialize device info.
    {
        m_info.apiName = "WGPU";
        m_info.deviceType = DeviceType::WGPU;
        m_info.adapterName = "default";
        static const float kIdentity[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        ::memcpy(m_info.identityProjectionMatrix, kIdentity, sizeof(kIdentity));
    }

    m_desc = desc;

    SLANG_RETURN_ON_FAIL(Device::initialize(desc));
    SLANG_RETURN_ON_FAIL(
        m_slangContext.initialize(desc.slang, SLANG_WGSL, "", std::array{slang::PreprocessorMacroDesc{"__WGPU__", "1"}})
    );

    const std::vector<const char*> enabledToggles = {"use_dxc"};
    WGPUDawnTogglesDescriptor togglesDesc = {};
    togglesDesc.chain.sType = WGPUSType_DawnTogglesDescriptor;
    togglesDesc.enabledToggleCount = enabledToggles.size();
    togglesDesc.enabledToggles = enabledToggles.data();

    WGPUInstanceDescriptor instanceDesc = {};
    instanceDesc.features.timedWaitAnyEnable = WGPUBool(true);
    instanceDesc.nextInChain = &togglesDesc.chain;
    m_ctx.instance = api.wgpuCreateInstance(&instanceDesc);

    // Request adapter.
    WGPURequestAdapterOptions options = {};
    options.powerPreference = WGPUPowerPreference_HighPerformance;
#if SLANG_WINDOWS_FAMILY
    // TODO(webgpu-d3d): New validation error in D3D kills webgpu, so use vulkan for now.
    options.backendType = WGPUBackendType_Vulkan;
#elif SLANG_LINUX_FAMILY
    options.backendType = WGPUBackendType_Vulkan;
#endif
    options.nextInChain = &togglesDesc.chain;

    {
        WGPURequestAdapterStatus status = WGPURequestAdapterStatus_Unknown;
        WGPURequestAdapterCallbackInfo2 callbackInfo = {};
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WGPURequestAdapterStatus status_,
                                   WGPUAdapter adapter,
                                   const char* message,
                                   void* userdata1,
                                   void* userdata2)
        {
            *(WGPURequestAdapterStatus*)userdata1 = status_;
            *(WGPUAdapter*)userdata2 = adapter;
        };
        callbackInfo.userdata1 = &status;
        callbackInfo.userdata2 = &m_ctx.adapter;
        WGPUFuture future = m_ctx.api.wgpuInstanceRequestAdapter2(m_ctx.instance, &options, callbackInfo);
        WGPUFutureWaitInfo futures[1] = {{future}};
        uint64_t timeoutNS = UINT64_MAX;
        WGPUWaitStatus waitStatus =
            m_ctx.api.wgpuInstanceWaitAny(m_ctx.instance, SLANG_COUNT_OF(futures), futures, timeoutNS);
        if (waitStatus != WGPUWaitStatus_Success || status != WGPURequestAdapterStatus_Success)
        {
            return SLANG_FAIL;
        }
    }

    // Query adapter limits.
    WGPUSupportedLimits adapterLimits = {};
    api.wgpuAdapterGetLimits(m_ctx.adapter, &adapterLimits);

    // Query adapter features.
    size_t adapterFeatureCount = api.wgpuAdapterEnumerateFeatures(m_ctx.adapter, nullptr);
    std::vector<WGPUFeatureName> adapterFeatures(adapterFeatureCount);
    api.wgpuAdapterEnumerateFeatures(m_ctx.adapter, adapterFeatures.data());

    // We request a device with the maximum available limits and feature set.
    WGPURequiredLimits requiredLimits = {};
    requiredLimits.limits = adapterLimits.limits;
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.requiredFeatures = adapterFeatures.data();
    deviceDesc.requiredFeatureCount = adapterFeatures.size();
    deviceDesc.requiredLimits = &requiredLimits;
    deviceDesc.uncapturedErrorCallbackInfo.callback = errorCallback;
    deviceDesc.uncapturedErrorCallbackInfo.userdata = this;
    deviceDesc.nextInChain = &togglesDesc.chain;

    {
        WGPURequestDeviceStatus status = WGPURequestDeviceStatus_Unknown;
        WGPURequestDeviceCallbackInfo2 callbackInfo = {};
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback =
            [](WGPURequestDeviceStatus status_, WGPUDevice device, const char* message, void* userdata1, void* userdata2
            )
        {
            *(WGPURequestDeviceStatus*)userdata1 = status_;
            *(WGPUDevice*)userdata2 = device;
        };
        callbackInfo.userdata1 = &status;
        callbackInfo.userdata2 = &m_ctx.device;

        WGPUDeviceLostCallbackInfo2 deviceLostCallbackInfo = {};
        deviceLostCallbackInfo.callback = [](const WGPUDevice* device,
                                             WGPUDeviceLostReason reason,
                                             const char* message,
                                             void* userdata1,
                                             void* userdata2)
        {
            if (reason != WGPUDeviceLostReason_Destroyed)
            {
                DeviceImpl* deviceimpl = static_cast<DeviceImpl*>(userdata1);
                deviceimpl->handleError(WGPUErrorType_DeviceLost, message);
            }
        };
        deviceLostCallbackInfo.userdata1 = this;
        deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        deviceDesc.deviceLostCallbackInfo2 = deviceLostCallbackInfo;

        WGPUFuture future = m_ctx.api.wgpuAdapterRequestDevice2(m_ctx.adapter, &deviceDesc, callbackInfo);
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
    WGPUSupportedLimits supportedLimits = {};
    api.wgpuDeviceGetLimits(m_ctx.device, &supportedLimits);
    m_ctx.limits = supportedLimits.limits;

    m_info.limits.maxComputeDispatchThreadGroups[0] = m_ctx.limits.maxComputeWorkgroupSizeX;

    // Query device features.
    size_t deviceFeatureCount = api.wgpuDeviceEnumerateFeatures(m_ctx.device, nullptr);
    std::vector<WGPUFeatureName> deviceFeatures(deviceFeatureCount);
    api.wgpuDeviceEnumerateFeatures(m_ctx.device, deviceFeatures.data());
    m_ctx.features.insert(deviceFeatures.begin(), deviceFeatures.end());

    addFeature(Feature::HardwareDevice);
    addFeature(Feature::Surface);
    addFeature(Feature::ParameterBlock);
    addFeature(Feature::Rasterization);

    if (api.wgpuDeviceHasFeature(m_ctx.device, WGPUFeatureName_ShaderF16))
    {
        addFeature(Feature::Half);
    }

    // Create queue.
    m_queue = new CommandQueueImpl(this, QueueType::Graphics);
    return SLANG_OK;
}

const DeviceInfo& DeviceImpl::getDeviceInfo() const
{
    return m_info;
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
        WGPUQueueWorkDoneStatus status = WGPUQueueWorkDoneStatus_Unknown;
        WGPUQueueWorkDoneCallbackInfo2 callbackInfo = {};
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WGPUQueueWorkDoneStatus status_, void* userdata1, void* userdata2)
        { *(WGPUQueueWorkDoneStatus*)userdata1 = status_; };
        callbackInfo.userdata1 = &status;
        WGPUFuture future = m_ctx.api.wgpuQueueOnSubmittedWorkDone2(queue, callbackInfo);
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
        WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Unknown;
        WGPUBufferMapCallbackInfo2 callbackInfo = {};
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WGPUMapAsyncStatus status_, const char* message, void* userdata1, void* userdata2)
        { *(WGPUMapAsyncStatus*)userdata1 = status_; };
        callbackInfo.userdata1 = &status;
        WGPUFuture future = m_ctx.api.wgpuBufferMapAsync2(stagingBuffer, WGPUMapMode_Read, 0, size, callbackInfo);
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

Result DeviceImpl::getFormatSupport(Format format, FormatSupport* outFormatSupport)
{
    FormatSupport support = FormatSupport::None;

    if (translateTextureFormat(format) != WGPUTextureFormat_Undefined)
    {
        support |= FormatSupport::Texture;
        if (isDepthFormat(format))
            support |= FormatSupport::DepthStencil;
        support |= FormatSupport::RenderTarget;
        support |= FormatSupport::Blendable;
        support |= FormatSupport::ShaderLoad;
        support |= FormatSupport::ShaderSample;
        support |= FormatSupport::ShaderUavLoad;
        support |= FormatSupport::ShaderUavStore;
        support |= FormatSupport::ShaderAtomic;
    }
    if (translateVertexFormat(format) != WGPUVertexFormat(0))
    {
        support |= FormatSupport::VertexBuffer;
    }
    if (format == Format::R32Uint || format == Format::R16Uint)
    {
        support |= FormatSupport::IndexBuffer;
    }
    *outFormatSupport = support;
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

} // namespace rhi::wgpu
