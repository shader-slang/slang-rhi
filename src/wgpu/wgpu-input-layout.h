#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class InputLayoutImpl : public InputLayout
{
public:
    DeviceImpl* m_device;
    std::vector<WGPUVertexBufferLayout> m_vertexBufferLayouts;
    std::vector<std::vector<WGPUVertexAttribute>> m_vertexAttributes;
};

} // namespace rhi::wgpu
