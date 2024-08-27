// d3d11-vertex-layout.h
#pragma once

#include "d3d11-base.h"

#include <vector>

namespace gfx
{

using namespace Slang;

namespace d3d11
{

class InputLayoutImpl: public InputLayoutBase
{
public:
	ComPtr<ID3D11InputLayout> m_layout;
    std::vector<UINT> m_vertexStreamStrides;
};

} // namespace d3d11
} // namespace gfx
