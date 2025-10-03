#pragma once

#include "wgpu-base.h"
#include "wgpu-command.h"

#include <unordered_set>

namespace rhi::wgpu {

struct Context
{
    API api;
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPULimits limits = {};
    std::unordered_set<WGPUFeatureName> features;

    ~Context();
};

class DeviceImpl : public Device
{
public:
    using Device::readBuffer;

    std::string m_adapterName;

    Context m_ctx;
    RefPtr<CommandQueueImpl> m_queue;

    ~DeviceImpl();
    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const DeviceDesc& desc) override;

    void reportError(const char* func, WGPUStringView message);
    void reportDeviceLost(WGPUDeviceLostReason reason, WGPUStringView message);
    void reportUncapturedError(WGPUErrorType type, WGPUStringView message);
    WGPUErrorType getAndClearLastUncapturedError();

    // IDevice implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createTexture(
        const TextureDesc& desc,
        const SubresourceData* initData,
        ITexture** outTexture
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createBuffer(
        const BufferDesc& desc,
        const void* initData,
        IBuffer** outBuffer
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createBufferFromNativeHandle(
        NativeHandle handle,
        const BufferDesc& desc,
        IBuffer** outBuffer
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(const SamplerDesc& desc, ISampler** outSampler) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unmapBuffer(IBuffer* buffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createTextureView(
        ITexture* texture,
        const TextureViewDesc& desc,
        ITextureView** outView
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createInputLayout(
        const InputLayoutDesc& desc,
        IInputLayout** outLayout
    ) override;

    virtual Result createShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayout** outLayout
    ) override;

    virtual Result createRootShaderObjectLayout(
        slang::IComponentType* program,
        slang::ProgramLayout* programLayout,
        ShaderObjectLayout** outLayout
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderTable(
        const ShaderTableDesc& desc,
        IShaderTable** outShaderTable
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createRenderPipeline2(
        const RenderPipelineDesc& desc,
        IRenderPipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createComputePipeline2(
        const ComputePipelineDesc& desc,
        IComputePipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createRayTracingPipeline2(
        const RayTracingPipelineDesc& desc,
        IRayTracingPipeline** outPipeline
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL readBuffer(
        IBuffer* buffer,
        Offset offset,
        Size size,
        void* outData
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureAllocationInfo(
        const TextureDesc& desc,
        Size* outSize,
        Size* outAlignment
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getTextureRowAlignment(Format format, Size* outAlignment) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createFence(const FenceDesc& desc, IFence** outFence) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL waitForFences(
        uint32_t fenceCount,
        IFence** fences,
        const uint64_t* fenceValues,
        bool waitForAll,
        uint64_t timeout
    ) override;

    // void waitForGpu();
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeDeviceHandles(DeviceNativeHandles* outHandles) override;

private:
    WGPUErrorType m_lastUncapturedError = WGPUErrorType_NoError;

    void initializeFormatSupport();
};

} // namespace rhi::wgpu

namespace rhi {

IAdapter* getWGPUAdapter(uint32_t index);
Result createWGPUDevice(const DeviceDesc* desc, IDevice** outDevice);

} // namespace rhi
