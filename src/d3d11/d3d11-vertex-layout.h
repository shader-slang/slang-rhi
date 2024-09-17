#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class InputLayoutImpl : public InputLayout
{
public:
    ComPtr<ID3D11InputLayout> m_layout;
    std::vector<UINT> m_vertexStreamStrides;
};

} // namespace rhi::d3d11
