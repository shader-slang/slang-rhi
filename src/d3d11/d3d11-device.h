#pragma once

#include "d3d11-pipeline.h"
#include "d3d11-texture-view.h"

namespace rhi::d3d11 {

class DeviceImpl : public ImmediateRendererBase
{
public:
    ~DeviceImpl() {}

    // Renderer    implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL initialize(const Desc& desc) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createSwapchain(const ISwapchain::Desc& desc, WindowHandle window, ISwapchain** outSwapchain) override;
    virtual void beginRenderPass(const RenderPassDesc& desc) override;
    virtual void endRenderPass() override;
    virtual void setStencilReference(uint32_t referenceValue) override;

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
    virtual void bindRootShaderObject(IShaderObject* shaderObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL createShaderProgram(
        const ShaderProgramDesc& desc,
        IShaderProgram** outProgram,
        ISlangBlob** outDiagnosticBlob
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createRenderPipeline(const RenderPipelineDesc& desc, IPipeline** outPipeline) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createComputePipeline(const ComputePipelineDesc& desc, IPipeline** outPipeline) override;

    virtual void* map(IBuffer* buffer, MapFlavor flavor) override;
    virtual void unmap(IBuffer* buffer, size_t offsetWritten, size_t sizeWritten) override;
    virtual void copyBuffer(IBuffer* dst, size_t dstOffset, IBuffer* src, size_t srcOffset, size_t size) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    readTexture(ITexture* texture, ResourceState state, ISlangBlob** outBlob, size_t* outRowPitch, size_t* outPixelSize)
        override;

    virtual void setPrimitiveTopology(PrimitiveTopology topology) override;

    virtual void setVertexBuffers(
        GfxIndex startSlot,
        GfxCount slotCount,
        IBuffer* const* buffers,
        const Offset* offsets
    ) override;
    virtual void setIndexBuffer(IBuffer* buffer, Format indexFormat, Offset offset) override;
    virtual void setViewports(GfxCount count, Viewport const* viewports) override;
    virtual void setScissorRects(GfxCount count, ScissorRect const* rects) override;
    virtual void setPipeline(IPipeline* state) override;
    virtual void draw(GfxCount vertexCount, GfxIndex startVertex) override;
    virtual void drawIndexed(GfxCount indexCount, GfxIndex startIndex, GfxIndex baseVertex) override;
    virtual void drawInstanced(
        GfxCount vertexCount,
        GfxCount instanceCount,
        GfxIndex startVertex,
        GfxIndex startInstanceLocation
    ) override;
    virtual void drawIndexedInstanced(
        GfxCount indexCount,
        GfxCount instanceCount,
        GfxIndex startIndexLocation,
        GfxIndex baseVertexLocation,
        GfxIndex startInstanceLocation
    ) override;
    virtual void dispatchCompute(int x, int y, int z) override;
    virtual void submitGpuWork() override {}
    virtual void waitForGpu() override {}
    virtual SLANG_NO_THROW const DeviceInfo& SLANG_MCALL getDeviceInfo() const override { return m_info; }
    virtual void beginCommandBuffer(const CommandBufferInfo& info) override;
    virtual void endCommandBuffer(const CommandBufferInfo& info) override;
    virtual void writeTimestamp(IQueryPool* pool, GfxIndex index) override;

public:
    void _flushGraphicsState();

    // D3D11Device members.

    DeviceInfo m_info;
    std::string m_adapterName;

    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_immediateContext;
    ComPtr<ID3D11Texture2D> m_backBufferTexture;
    ComPtr<IDXGIFactory> m_dxgiFactory;

    short_vector<ID3D11RenderTargetView*> m_d3dRenderTargetViews;
    ID3D11DepthStencilView* m_d3dDepthStencilView;

    RefPtr<PipelineImpl> m_currentPipeline;

    ComPtr<ID3D11Query> m_disjointQuery;

    uint32_t m_stencilRef = 0;
    bool m_depthStencilStateDirty = true;

    Desc m_desc;

    float m_clearColor[4] = {0, 0, 0, 0};

    bool m_nvapi = false;
};

} // namespace rhi::d3d11
