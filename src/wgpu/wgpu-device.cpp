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

    const std::vector<const char*> enabledToggles = {
        "use_dxc",
        // "d3d12_force_clear_copyable_depth_stencil_texture_on_creation",
    };
    const std::vector<const char*> disabledToggles = {
        "d3d12_create_not_zeroed_heap",
    };
    WGPUDawnTogglesDescriptor togglesDesc = {};
    togglesDesc.chain.sType = WGPUSType_DawnTogglesDescriptor;
    togglesDesc.enabledToggleCount = enabledToggles.size();
    togglesDesc.enabledToggles = enabledToggles.data();
    togglesDesc.disabledToggleCount = disabledToggles.size();
    togglesDesc.disabledToggles = disabledToggles.data();

    WGPUInstanceDescriptor instanceDesc = {};
    instanceDesc.capabilities.timedWaitAnyEnable = WGPUBool(true);
    instanceDesc.nextInChain = &togglesDesc.chain;
    m_ctx.instance = api.wgpuCreateInstance(&instanceDesc);

    // Request adapter.
    WGPURequestAdapterOptions options = {};
    options.powerPreference = WGPUPowerPreference_HighPerformance;
#if SLANG_WINDOWS_FAMILY
    // TODO(webgpu-d3d): New validation error in D3D kills webgpu, so use vulkan for now.
    options.backendType = WGPUBackendType_D3D12;
#elif SLANG_LINUX_FAMILY
    options.backendType = WGPUBackendType_Vulkan;
#endif
    options.nextInChain = &togglesDesc.chain;

    {
        WGPURequestAdapterStatus status = WGPURequestAdapterStatus(0);
        WGPURequestAdapterCallbackInfo callbackInfo = {};
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WGPURequestAdapterStatus status_,
                                   WGPUAdapter adapter,
                                   WGPUStringView message,
                                   void* userdata1,
                                   void* userdata2)
        {
            *(WGPURequestAdapterStatus*)userdata1 = status_;
            *(WGPUAdapter*)userdata2 = adapter;
        };
        callbackInfo.userdata1 = &status;
        callbackInfo.userdata2 = &m_ctx.adapter;
        WGPUFuture future = m_ctx.api.wgpuInstanceRequestAdapter(m_ctx.instance, &options, callbackInfo);
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

    m_info.limits.maxComputeDispatchThreadGroups[0] = m_ctx.limits.maxComputeWorkgroupSizeX;

    // Query device features.
    WGPUSupportedFeatures supportedFeatures = {};
    api.wgpuDeviceGetFeatures(m_ctx.device, &supportedFeatures);
    m_ctx.features.insert(supportedFeatures.features, supportedFeatures.features + supportedFeatures.featureCount);

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
        WGPUQueueWorkDoneStatus status = WGPUQueueWorkDoneStatus(0);
        WGPUQueueWorkDoneCallbackInfo callbackInfo = {};
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WGPUQueueWorkDoneStatus status_, void* userdata1, void* userdata2)
        { *(WGPUQueueWorkDoneStatus*)userdata1 = status_; };
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
