#pragma once

#include "d3d11-base.h"
#include "d3d11-shader-object-layout.h"

namespace rhi::d3d11 {

class ShaderProgramImpl : public ShaderProgram
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11ComputeShader> m_computeShader;

    virtual ShaderObjectLayout* getRootShaderObjectLayout() override { return m_rootObjectLayout; }
};

} // namespace rhi::d3d11
