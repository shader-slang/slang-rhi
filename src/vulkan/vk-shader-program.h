#pragma once

#include "vk-base.h"

#include <vector>

namespace rhi::vk {

class ShaderProgramImpl : public ShaderProgram
{
public:
    ShaderProgramImpl(DeviceImpl* device);

    ~ShaderProgramImpl();

    virtual void comFree() override;

    BreakableReference<DeviceImpl> m_device;

    RefPtr<RootShaderObjectLayoutImpl> m_rootShaderObjectLayout;

    struct Module
    {
        ComPtr<ISlangBlob> code;
        std::string entryPointName;
        VkShaderModule shaderModule;
        bool hasBindlessDescriptorSet;
        uint32_t bindlessDescriptorSet;
    };

    std::vector<Module> m_modules;
    std::vector<VkPipelineShaderStageCreateInfo> m_stageCreateInfos;

    virtual Result createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
        override;

    virtual ShaderObjectLayout* getRootShaderObjectLayout() override;
};

} // namespace rhi::vk
