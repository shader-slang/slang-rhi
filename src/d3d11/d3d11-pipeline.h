#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class RenderPipelineImpl : public RenderPipelineBase
{
public:
    UINT m_rtvCount;

    RefPtr<InputLayoutImpl> m_inputLayout;
    ComPtr<ID3D11DepthStencilState> m_depthStencilState;
    ComPtr<ID3D11RasterizerState> m_rasterizerState;
    ComPtr<ID3D11BlendState> m_blendState;

    float m_blendColor[4];
    UINT m_sampleMask;

    Result init(const RenderPipelineDesc& desc);
};

class ComputePipelineImpl : public ComputePipelineBase
{
public:
    Result init(const ComputePipelineDesc& desc);
};

} // namespace rhi::d3d11
