#include "testing.h"

#if SLANG_RHI_ENABLE_VULKAN

#include "../src/vulkan/vk-utils.h"

using namespace rhi;

// Read-only shader access (storage buffers, uniform texel buffers, sampled images) must be covered
// by VK_ACCESS_SHADER_READ_BIT, or a barrier into ShaderResource won't make prior writes visible to
// the shader read. Regression for shader-slang/slang-rhi#859.
TEST_CASE("vk-calc-access-flags-shader-resource")
{
    VkAccessFlags flags = vk::calcAccessFlags(ResourceState::ShaderResource);
    CHECK((flags & VK_ACCESS_SHADER_READ_BIT) != 0);
    CHECK((flags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT) != 0);
}

#endif // SLANG_RHI_ENABLE_VULKAN
