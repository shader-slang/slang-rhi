#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class InputLayoutImpl : public InputLayoutBase
{
public:
    std::vector<D3D12_INPUT_ELEMENT_DESC> m_elements;
    std::vector<UINT> m_vertexStreamStrides;
    std::vector<char> m_text; ///< Holds all strings to keep in scope
};

} // namespace rhi::d3d12
