#pragma once

#include "cpu-base.h"
#include "cpu-command.h"
#include "cpu-pipeline.h"
#include "cpu-shader-object.h"

namespace rhi::cpu {

class DeviceImpl : public Device
{
public:
    ~DeviceImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const DeviceDesc& desc) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBuffer(const BufferDesc& descIn, const void* initData, IBuffer** outBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL unmapBuffer(IBuffer* buffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureView(ITexture* inTexture, const TextureViewDesc& desc, ITextureView** outView) override;

    virtual Result createShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayout** outLayout
    ) override;

    virtual Result createShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject) override;

    virtual Result createRootShaderObject(IShaderProgram* program, IShaderObject** outObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool) override;

    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getDeviceInfo() const override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(SamplerDesc const& desc, ISampler** outSampler) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readTexture(ITexture* texture, ISlangBlob** outBlob, Size* outRowPitch, Size* outPixelSize) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readBuffer(IBuffer* buffer, Offset offset, Size size, ISlangBlob** outBlob) override;

private:
    DeviceInfo m_info;

    RefPtr<CommandQueueImpl> m_queue;
};

} // namespace rhi::cpu

namespace rhi {

Result SLANG_MCALL createCPUDevice(const DeviceDesc* desc, IDevice** outDevice);

} // namespace rhi
