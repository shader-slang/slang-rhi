#pragma once

#include "webgpu-base.h"

namespace rhi::webgpu {

class InputLayoutImpl : public InputLayout
{
public:
    DeviceImpl* m_device;
    std::vector<WebGPUVertexBufferLayout> m_vertexBufferLayouts;
    std::vector<std::vector<WebGPUVertexAttribute>> m_vertexAttributes;
};

} // namespace rhi::webgpu
