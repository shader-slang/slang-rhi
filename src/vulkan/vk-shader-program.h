#pragma once

#include "vk-base.h"
#include "vk-shader-object-layout.h"

#include <vector>

namespace rhi::vk {

class ShaderProgramImpl : public ShaderProgramBase
{
public:
    ShaderProgramImpl(DeviceImpl* device);

    ~ShaderProgramImpl();

    virtual void comFree() override;

    BreakableReference<DeviceImpl> m_device;

    std::vector<VkPipelineShaderStageCreateInfo> m_stageCreateInfos;
    std::vector<std::string> m_entryPointNames;
    std::vector<ComPtr<ISlangBlob>> m_codeBlobs; //< To keep storage of code in scope
    std::vector<VkShaderModule> m_modules;
    RefPtr<RootShaderObjectLayout> m_rootObjectLayout;

    VkPipelineShaderStageCreateInfo compileEntryPoint(
        const char* entryPointName,
        ISlangBlob* code,
        VkShaderStageFlagBits stage,
        VkShaderModule& outShaderModule
    );

    virtual Result createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
        override;
};

} // namespace rhi::vk
