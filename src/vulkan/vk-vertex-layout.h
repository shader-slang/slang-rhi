// vk-vertex-layout.h
#pragma once

#include "vk-base.h"

#include <vector>

namespace rhi
{

using namespace Slang;

namespace vk
{

class InputLayoutImpl : public InputLayoutBase
{
public:
    std::vector<VkVertexInputAttributeDescription> m_attributeDescs;
    std::vector<VkVertexInputBindingDescription> m_streamDescs;
};

} // namespace vk
} // namespace rhi
