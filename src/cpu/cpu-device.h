#pragma once

#include "cpu-base.h"
#include "cpu-pipeline.h"
#include "cpu-shader-object.h"

namespace rhi::cpu {

class DeviceImpl : public ImmediateComputeDeviceBase
{
public:
    ~DeviceImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const Desc& desc) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBuffer(const BufferDesc& descIn, const void* initData, IBuffer** outBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureView(ITexture* inTexture, IResourceView::Desc const& desc, IResourceView** outView) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createBufferView(
        IBuffer* inBuffer,
        IBuffer* counterBuffer,
        IResourceView::Desc const& desc,
        IResourceView** outView
    ) override;

    virtual Result createShaderObjectLayout(
        slang::ISession* session,
        slang::TypeLayoutReflection* typeLayout,
        ShaderObjectLayoutBase** outLayout
    ) override;

    virtual Result createShaderObject(ShaderObjectLayoutBase* layout, IShaderObject** outObject) override;

    virtual Result createMutableShaderObject(ShaderObjectLayoutBase* layout, IShaderObject** outObject) override;

    virtual Result createRootShaderObject(IShaderProgram* program, ShaderObjectBase** outObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createProgram(
        const IShaderProgram::Desc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline(const ComputePipelineDesc& desc, IPipeline** outPipeline) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createQueryPool(const IQueryPool::Desc& desc, IQueryPool** outPool) override;

    virtual void writeTimestamp(IQueryPool* pool, GfxIndex index) override;

    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getDeviceInfo() const override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(SamplerDesc const& desc, ISampler** outSampler) override;

    virtual void submitGpuWork() override {}
    virtual void waitForGpu() override {}
    virtual void* map(IBuffer* buffer, MapFlavor flavor) override;
    virtual void unmap(IBuffer* buffer, size_t offsetWritten, size_t sizeWritten) override;

private:
    RefPtr<PipelineImpl> m_currentPipeline = nullptr;
    RefPtr<RootShaderObjectImpl> m_currentRootObject = nullptr;
    DeviceInfo m_info;

    virtual void setPipeline(IPipeline* state) override;

    virtual void bindRootShaderObject(IShaderObject* object) override;

    virtual void dispatchCompute(int x, int y, int z) override;

    virtual void copyBuffer(IBuffer* dst, size_t dstOffset, IBuffer* src, size_t srcOffset, size_t size) override;
};

} // namespace rhi::cpu

namespace rhi {

Result SLANG_MCALL createCPUDevice(const IDevice::Desc* desc, IDevice** outDevice);

} // namespace rhi
