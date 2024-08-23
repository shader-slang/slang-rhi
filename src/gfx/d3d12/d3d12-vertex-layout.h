// d3d12-vertex-layout.h
#pragma once

#include "d3d12-base.h"

namespace gfx
{
namespace d3d12
{

using namespace Slang;

class InputLayoutImpl : public InputLayoutBase
{
public:
    std::vector<D3D12_INPUT_ELEMENT_DESC> m_elements;
    std::vector<UINT> m_vertexStreamStrides;
    std::vector<char> m_text; ///< Holds all strings to keep in scope
};

} // namespace d3d12
} // namespace gfx
