#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class ShaderProgramImpl : public ShaderProgram
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;

    struct Module
    {
        SlangStage stage;
        std::string entryPointName;
        std::string code;
        WGPUShaderModule module = nullptr;
    };

    std::vector<Module> m_modules;

    ShaderProgramImpl(Device* device, const ShaderProgramDesc& desc);
    ~ShaderProgramImpl();

    virtual Result createShaderModule(
        slang::EntryPointReflection* entryPointInfo,
        ComPtr<ISlangBlob> kernelCode
    ) override;

    virtual ShaderObjectLayout* getRootShaderObjectLayout() override;

    Module* findModule(SlangStage stage);
};

} // namespace rhi::wgpu
