#pragma once

#include "metal-base.h"

#include <vector>

namespace rhi::metal {

class InputLayoutImpl : public InputLayout
{
public:
    std::vector<InputElementDesc> m_inputElements;
    std::vector<VertexStreamDesc> m_vertexStreams;

    Result init(const InputLayoutDesc& desc);
    NS::SharedPtr<MTL::VertexDescriptor> createVertexDescriptor(NS::UInteger vertexBufferIndexOffset);
};

} // namespace rhi::metal
