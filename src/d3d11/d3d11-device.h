#pragma once

#include "d3d11-pipeline.h"
#include "d3d11-texture-view.h"
#include "d3d11-shader-object.h"

namespace rhi::d3d11 {

class DeviceImpl : public ImmediateDevice
{
public:
    ~DeviceImpl() {}

    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const DeviceDesc& desc) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSurface(WindowHandle windowHandle, ISurface** outSurface) override;
    virtual void beginRenderPass(const RenderPassDesc& desc) override;
    virtual void endRenderPass() override;
    virtual void setRenderState(const RenderState& state) override;
    virtual void draw(const DrawArguments& args) override;
    virtual void drawIndexed(const DrawArguments& args) override;
    virtual void setComputeState(const ComputeState& state) override;
    virtual void dispatchCompute(int x, int y, int z) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTexture(const TextureDesc& desc, const SubresourceData* initData, ITexture** outTexture) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createBuffer(const BufferDesc& desc, const void* initData, IBuffer** outBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL createSampler(SamplerDesc const& desc, ISampler** outSampler) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createInputLayout(InputLayoutDesc const& desc, IInputLayout** outLayout) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool) override;

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
    createRenderPipeline2(const RenderPipelineDesc2& desc, IRenderPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline2(const ComputePipelineDesc2& desc, IComputePipeline** outPipeline) override;

    virtual void* map(IBuffer* buffer, MapFlavor flavor) override;
    virtual void unmap(IBuffer* buffer, size_t offsetWritten, size_t sizeWritten) override;
    virtual void copyBuffer(IBuffer* dst, size_t dstOffset, IBuffer* src, size_t srcOffset, size_t size) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    readTexture(ITexture* texture, ISlangBlob** outBlob, size_t* outRowPitch, size_t* outPixelSize) override;

    virtual void submitGpuWork() override {}
    virtual void waitForGpu() override {}
    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getDeviceInfo() const override { return m_info; }
    virtual void beginCommandBuffer(const CommandBufferInfo& info) override;
    virtual void endCommandBuffer(const CommandBufferInfo& info) override;
    virtual void writeTimestamp(IQueryPool* pool, GfxIndex index) override;

public:
    void clearState();

    // D3D11Device members.

    DeviceInfo m_info;
    std::string m_adapterName;

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_immediateContext;
    ComPtr<IDXGIFactory> m_dxgiFactory;

    short_vector<RefPtr<TextureViewImpl>> m_renderTargetViews;
    short_vector<RefPtr<TextureViewImpl>> m_resolveTargetViews;
    RefPtr<TextureViewImpl> m_depthStencilView;

    bool m_renderPassValid = false;
    bool m_renderStateValid = false;
    RenderState m_renderState = {};
    RefPtr<RenderPipelineImpl> m_renderPipeline;

    bool m_computeStateValid = false;
    ComputeState m_computeState = {};
    RefPtr<ComputePipelineImpl> m_computePipeline;

    RefPtr<RootShaderObjectImpl> m_rootObject;

    ComPtr<ID3D11Query> m_disjointQuery;

    DeviceDesc m_desc;

    float m_clearColor[4] = {0, 0, 0, 0};

    bool m_nvapi = false;
};

} // namespace rhi::d3d11
