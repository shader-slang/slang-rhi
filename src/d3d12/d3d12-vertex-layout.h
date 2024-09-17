#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class InputLayoutImpl : public InputLayout
{
public:
    std::vector<D3D12_INPUT_ELEMENT_DESC> m_elements;
    std::vector<UINT> m_vertexStreamStrides;
    /// Holds all strings to keep in scope.
    std::vector<char> m_text;
};

} // namespace rhi::d3d12
