#pragma once

#include "cpu-base.h"
#include "cpu-pipeline.h"
#include "cpu-shader-object.h"

namespace rhi::cpu {

class DeviceImpl : public ImmediateComputeDeviceBase
{
public:
    ~DeviceImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const DeviceDesc& desc) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBuffer(const BufferDesc& descIn, const void* initData, IBuffer** outBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureView(ITexture* inTexture, const TextureViewDesc& desc, ITextureView** outView) override;

    virtual Result createShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayout** outLayout
    ) override;

    virtual Result createShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject) override;

    virtual Result createMutableShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject) override;

    virtual Result createRootShaderObject(IShaderProgram* program, ShaderObjectBase** outObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline2(const ComputePipelineDesc2& desc, IComputePipeline** outPipeline) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool) override;

    virtual void writeTimestamp(IQueryPool* pool, GfxIndex index) override;

    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getDeviceInfo() const override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(SamplerDesc const& desc, ISampler** outSampler) override;

    virtual void submitGpuWork() override {}
    virtual void waitForGpu() override {}
    virtual void* map(IBuffer* buffer, MapFlavor flavor) override;
    virtual void unmap(IBuffer* buffer, size_t offsetWritten, size_t sizeWritten) override;

private:
    RefPtr<Pipeline> m_currentPipeline = nullptr;
    RefPtr<RootShaderObjectImpl> m_currentRootObject = nullptr;
    DeviceInfo m_info;

    virtual void setPipeline(IPipeline* state) override;

    virtual void bindRootShaderObject(IShaderObject* object) override;

    virtual void dispatchCompute(int x, int y, int z) override;

    virtual void copyBuffer(IBuffer* dst, size_t dstOffset, IBuffer* src, size_t srcOffset, size_t size) override;
};

} // namespace rhi::cpu

namespace rhi {

Result SLANG_MCALL createCPUDevice(const DeviceDesc* desc, IDevice** outDevice);

} // namespace rhi
