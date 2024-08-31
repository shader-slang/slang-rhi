#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class PipelineImpl : public PipelineBase
{
public:
};

class GraphicsPipelineImpl : public PipelineImpl
{
public:
    UINT m_rtvCount;

    RefPtr<InputLayoutImpl> m_inputLayout;
    ComPtr<ID3D11DepthStencilState> m_depthStencilState;
    ComPtr<ID3D11RasterizerState> m_rasterizerState;
    ComPtr<ID3D11BlendState> m_blendState;

    float m_blendColor[4];
    UINT m_sampleMask;

    void init(const RenderPipelineDesc& inDesc);
};

class ComputePipelineImpl : public PipelineImpl
{
public:
    void init(const ComputePipelineDesc& inDesc);
};

} // namespace rhi::d3d11
