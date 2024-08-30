#pragma once

#include "vk-base.h"

#include <vector>

namespace rhi::vk {

class InputLayoutImpl : public InputLayoutBase
{
public:
    std::vector<VkVertexInputAttributeDescription> m_attributeDescs;
    std::vector<VkVertexInputBindingDescription> m_streamDescs;
};

} // namespace rhi::vk
