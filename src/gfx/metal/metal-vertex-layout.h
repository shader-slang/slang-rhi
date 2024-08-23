// metal-vertex-layout.h
#pragma once

#include "metal-base.h"

#include <vector>

namespace gfx
{

using namespace Slang;

namespace metal
{

class InputLayoutImpl : public InputLayoutBase
{
public:
    std::vector<InputElementDesc> m_inputElements;
    std::vector<VertexStreamDesc> m_vertexStreams;

    Result init(const IInputLayout::Desc& desc);
    NS::SharedPtr<MTL::VertexDescriptor> createVertexDescriptor(NS::UInteger vertexBufferIndexOffset);
};

} // namespace metal
} // namespace gfx
